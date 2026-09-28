"""FreeDict (freedict.org) TEI P5 dictionaries, PLAN.md 6.2.

FreeDict publishes one TEI P5 XML file per `<src>-<tgt>` language pair, listed
in a single database index (`freedict-database.json`) with one or more
release artifacts per dictionary. This converter uses the release whose
`platform` is `"src"`: a `.src.tar.xz` holding the TEI source plus its
licence.

Mapping decisions (DOCS/sources.md, "FreeDict notes"):
- eligibility is decided from the database index alone: a dictionary needs
  at least 1000 headwords (the `headwords` field) and a licence the TEI
  header maps to a known SPDX id (`detect_license`). Anything else is
  excluded and counted by reason, never guessed;
- ISO 639-3 codes (FreeDict's `<src>-<tgt>` name) map to ISO 639-1 where one
  exists (`_LANG_TABLE`), else the 3-letter code is kept;
- consecutive `<entry>` elements sharing a headword merge into one entry,
  the same rule Kaikki uses, because FreeDict lists one `<entry>` per sense
  group (e.g. `Bank.1`, `Bank.2`, `Bank.3`);
- `<gramGrp><pos>`/`<gen>` are entry-level and apply to every `<sense>` of
  that `<entry>`; `<gramGrp><gen>` becomes the sense `pattern`
  ("masculine", "feminine", "neuter");
- a sense's definition is its `<cit type="trans"><quote>` translations,
  joined by ", ", with a `<def>` element's text appended in parentheses
  when present; a sense with neither is dropped;
- `<usg>` becomes the sense `label`; `<cit type="example">` becomes an
  example, its nested `<cit type="trans">` the translation;
- `<xr>` references become relations (`syn` -> synonym, `ant` -> antonym,
  `see`/anything else -> see), the ref's visible text as the target;
- `<form><pron>` becomes a pronunciation only when it looks like IPA
  (`_looks_like_ipa`); FreeDict has no separate plain-transcription field,
  so a pronunciation that is not IPA-like is dropped rather than guessed at.

Eligibility and licence detection query the TEI header only (streamed out of
the network response for the `--list` scan, or out of the cached tarball
once fetched) and are cached in `<cache>/header-cache.json` keyed by
`name@edition`, so a repeat `--list` costs no network calls until FreeDict
publishes a new edition.

Run: `python -m omnipipe.converters.freedict --list` or
`python -m omnipipe.converters.freedict --dict freedict-de-en --out out/`.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import re
import shutil
import sys
import tarfile
import urllib.error
import urllib.request
from collections import Counter
from collections.abc import Iterable, Iterator, Mapping, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from datetime import UTC, datetime
from itertools import islice
from pathlib import Path
from typing import IO, ClassVar
from xml.etree import ElementTree as ET
from xml.etree.ElementTree import Element

from omnipipe.build import BuildError, build_bundle
from omnipipe.converters.base import Converter
from omnipipe.schema import (
    POS_TAGS,
    DictSpec,
    Entry,
    Example,
    Form,
    Pronunciation,
    Relation,
    SchemaError,
    Sense,
)

logger = logging.getLogger(__name__)

FREEDICT_DATABASE_URL = "https://freedict.org/freedict-database.json"
USER_AGENT = "omnipipe/0.1 (+https://github.com/keshavbhatt/omnidict)"
TEI_NS = "http://www.tei-c.org/ns/1.0"

_MIN_HEADWORDS = 1000
_MAX_EXAMPLES = 20
_HEADER_PEEK_CAP = 1 << 18  # 256 KiB: generous for a teiHeader, small next to any dump
_WS_RE = re.compile(r"\s+")
_VERSION_RE = re.compile(r"^\d+(\.\d+)*$")

_REASON_TOO_FEW = "too few headwords"
_REASON_NO_LICENCE = "no recognised free licence"
_REASON_FETCH_ERROR = "header unreachable"

type Json = Mapping[str, object]


class FreeDictError(Exception):
    """Raised when the database index, a header, or a dictionary tarball is unusable."""


# ---------------------------------------------------------------------------
# Language codes: ISO 639-3 (FreeDict's `<src>-<tgt>` name) -> (code, English name).
# `code` is the ISO 639-1 two-letter form where one exists, else the 639-3 code
# itself. Covers every code seen in freedict-database.json as of 2026-09-28;
# `lang_code`/`lang_name` raise if FreeDict adds a language outside this table,
# rather than silently mis-mapping it.
# ---------------------------------------------------------------------------

_LANG_TABLE: Mapping[str, tuple[str, str]] = {
    "afr": ("af", "Afrikaans"),
    "ara": ("ar", "Arabic"),
    "ast": ("ast", "Asturian"),
    "bre": ("br", "Breton"),
    "bul": ("bg", "Bulgarian"),
    "cat": ("ca", "Catalan"),
    "ces": ("cs", "Czech"),
    "ckb": ("ckb", "Central Kurdish"),
    "cym": ("cy", "Welsh"),
    "dan": ("da", "Danish"),
    "deu": ("de", "German"),
    "ell": ("el", "Greek"),
    "eng": ("en", "English"),
    "epo": ("eo", "Esperanto"),
    "fin": ("fi", "Finnish"),
    "fra": ("fr", "French"),
    "gla": ("gd", "Scottish Gaelic"),
    "gle": ("ga", "Irish"),
    "hin": ("hi", "Hindi"),
    "hrv": ("hr", "Croatian"),
    "hun": ("hu", "Hungarian"),
    "ind": ("id", "Indonesian"),
    "isl": ("is", "Icelandic"),
    "ita": ("it", "Italian"),
    "jpn": ("ja", "Japanese"),
    "kha": ("kha", "Khasi"),
    "kmr": ("kmr", "Northern Kurdish"),
    "kur": ("ku", "Kurdish"),
    "lat": ("la", "Latin"),
    "lit": ("lt", "Lithuanian"),
    "mkd": ("mk", "Macedonian"),
    "mlg": ("mg", "Malagasy"),
    "nld": ("nl", "Dutch"),
    "nno": ("nn", "Norwegian Nynorsk"),
    "nob": ("nb", "Norwegian Bokmal"),
    "nor": ("no", "Norwegian"),
    "oci": ("oc", "Occitan"),
    "pol": ("pl", "Polish"),
    "por": ("pt", "Portuguese"),
    "rom": ("rom", "Romani"),
    "rus": ("ru", "Russian"),
    "san": ("sa", "Sanskrit"),
    "slk": ("sk", "Slovak"),
    "slv": ("sl", "Slovenian"),
    "spa": ("es", "Spanish"),
    "srp": ("sr", "Serbian"),
    "swe": ("sv", "Swedish"),
    "swh": ("sw", "Swahili"),
    "tur": ("tr", "Turkish"),
    "wol": ("wo", "Wolof"),
    "zho": ("zh", "Chinese"),
}


def lang_code(code3: str) -> str:
    try:
        return _LANG_TABLE[code3][0]
    except KeyError:
        raise FreeDictError(
            f"{code3!r}: unknown FreeDict language code, add it to _LANG_TABLE"
        ) from None


def lang_name(code3: str) -> str:
    try:
        return _LANG_TABLE[code3][1]
    except KeyError:
        raise FreeDictError(
            f"{code3!r}: unknown FreeDict language code, add it to _LANG_TABLE"
        ) from None


# ---------------------------------------------------------------------------
# The database index (freedict-database.json)
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class DatabaseEntry:
    """One dictionary listed in freedict-database.json, with its `src` release."""

    name: str  # "deu-eng"
    src_lang3: str
    tgt_lang3: str
    edition: str
    headwords: int
    src_url: str
    src_sha512: str


def _src_release(obj: Json) -> Json | None:
    releases = obj.get("releases")
    if not isinstance(releases, list):
        return None
    for item in releases:
        if isinstance(item, Mapping) and item.get("platform") == "src":
            return item
    return None


def parse_database(obj: Sequence[object]) -> list[DatabaseEntry]:
    """Canonical dictionaries from the raw database JSON array.

    Entries without a `name` (FreeDict's own tooling metadata), without a
    two-part `<src>-<tgt>` name, or without a `src` release are silently
    skipped: they are not dictionaries this converter can build.
    """
    entries: list[DatabaseEntry] = []
    for item in obj:
        if not isinstance(item, Mapping):
            continue
        name = item.get("name")
        if not isinstance(name, str):
            continue
        parts = name.split("-")
        if len(parts) != 2 or not all(len(p) == 3 for p in parts):
            continue
        release = _src_release(item)
        if release is None:
            continue
        url = release.get("URL")
        checksum = release.get("checksum")
        if not isinstance(url, str) or not isinstance(checksum, str):
            continue
        edition = str(item.get("edition", "")).strip()
        if not edition:
            continue
        try:
            headwords = int(str(item.get("headwords", "0")).strip() or "0")
        except ValueError:
            headwords = 0
        entries.append(
            DatabaseEntry(
                name=name,
                src_lang3=parts[0],
                tgt_lang3=parts[1],
                edition=edition,
                headwords=headwords,
                src_url=url,
                src_sha512=checksum.lower(),
            )
        )
    return entries


@dataclass(frozen=True, slots=True)
class DatabaseInfo:
    """What was fetched, kept next to the index as `<file>.json`."""

    url: str
    last_modified: str | None
    fetched_at: str

    def to_json(self) -> dict[str, object]:
        return {"url": self.url, "last_modified": self.last_modified, "fetched_at": self.fetched_at}

    @staticmethod
    def from_json(obj: Json) -> DatabaseInfo:
        last_modified = obj.get("last_modified")
        return DatabaseInfo(
            url=str(obj["url"]),
            last_modified=str(last_modified) if last_modified is not None else None,
            fetched_at=str(obj["fetched_at"]),
        )


def database_path(cache_dir: Path) -> Path:
    return cache_dir / "freedict-database.json"


def read_database_info(path: Path) -> DatabaseInfo:
    info_path = path.with_name(path.name + ".json")
    if not info_path.exists():
        raise FreeDictError(f"{info_path} is missing: fetch the database first")
    obj = json.loads(info_path.read_text(encoding="utf-8"))
    if not isinstance(obj, Mapping):
        raise FreeDictError(f"{info_path} is not a JSON object")
    return DatabaseInfo.from_json(obj)


def fetch_database(cache_dir: Path, *, timeout: float = 60.0) -> Path:
    """Download the database index into `cache_dir` unless the cached copy is current."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    target = database_path(cache_dir)
    info_path = target.with_name(target.name + ".json")
    headers = {"User-Agent": USER_AGENT}
    previous = read_database_info(target) if target.exists() and info_path.exists() else None
    if previous is not None and previous.last_modified:
        headers["If-Modified-Since"] = previous.last_modified

    partial = target.with_name(target.name + ".part")
    request = urllib.request.Request(FREEDICT_DATABASE_URL, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            last_modified = response.headers.get("Last-Modified")
            with partial.open("wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)
    except urllib.error.HTTPError as exc:
        if exc.code == 304 and previous is not None:
            logger.info("%s is current", target)
            return target
        raise FreeDictError(f"cannot fetch {FREEDICT_DATABASE_URL}: HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise FreeDictError(f"cannot fetch {FREEDICT_DATABASE_URL}: {exc.reason}") from exc

    partial.replace(target)
    fetched_at = datetime.now(UTC).replace(microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")
    info = DatabaseInfo(FREEDICT_DATABASE_URL, last_modified, fetched_at)
    info_path.write_text(json.dumps(info.to_json(), indent=2) + "\n", encoding="utf-8")
    logger.info("fetched %s (%s)", target, last_modified)
    return target


def read_database(path: Path) -> list[DatabaseEntry]:
    if not path.exists():
        raise FreeDictError(f"{path} is missing: fetch the database first")
    obj = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(obj, list):
        raise FreeDictError(f"{path} is not a JSON array")
    return parse_database(obj)


def dict_id_for(entry: DatabaseEntry) -> str:
    return f"freedict-{lang_code(entry.src_lang3)}-{lang_code(entry.tgt_lang3)}"


def find_database_entry(entries: Sequence[DatabaseEntry], dict_id: str) -> DatabaseEntry:
    for entry in entries:
        if dict_id_for(entry) == dict_id:
            return entry
    raise FreeDictError(f"unknown dictionary {dict_id!r}; try --list")


def version_from_edition(edition: str) -> str:
    """The FreeDict edition as-is when it already fits the pipeline's version
    rule (dot-separated non-negative integers, `catalog._VERSION_RE`); else
    its digit groups joined by dots, e.g. "1.9-fd1" -> "1.9.1"."""
    if _VERSION_RE.match(edition):
        return edition
    digits = re.findall(r"\d+", edition)
    if not digits:
        raise FreeDictError(f"edition {edition!r} has no digits to build a version from")
    return ".".join(digits)


# ---------------------------------------------------------------------------
# Licence detection from the TEI header
# ---------------------------------------------------------------------------

# (URL pattern, base SPDX id, has an "-or-later"/"-only" distinction). Checked
# first, on the ref's `target`: these URLs already name a version.
_LICENSE_URL_PATTERNS: Sequence[tuple[re.Pattern[str], str, bool]] = (
    (re.compile(r"gnu\.org/licenses/(old-licenses/)?agpl-3\.0"), "AGPL-3.0", True),
    (re.compile(r"gnu\.org/licenses/(old-licenses/)?gpl-3\.0"), "GPL-3.0", True),
    (re.compile(r"gnu\.org/licenses/(old-licenses/)?gpl-2\.0"), "GPL-2.0", True),
    (re.compile(r"gnu\.org/licenses/(old-licenses/)?lgpl-3\.0"), "LGPL-3.0", True),
    (re.compile(r"gnu\.org/licenses/(old-licenses/)?lgpl-2\.1"), "LGPL-2.1", True),
    (re.compile(r"gnu\.org/licenses/fdl-1\.3"), "GFDL-1.3", True),
    (re.compile(r"gnu\.org/licenses/fdl-1\.2"), "GFDL-1.2", True),
    (re.compile(r"gnu\.org/licenses/fdl-1\.1"), "GFDL-1.1", True),
    (re.compile(r"creativecommons\.org/licenses/by-sa/4\.0"), "CC-BY-SA-4.0", False),
    (re.compile(r"creativecommons\.org/licenses/by-sa/3\.0"), "CC-BY-SA-3.0", False),
    (re.compile(r"creativecommons\.org/licenses/by-sa/2\.5"), "CC-BY-SA-2.5", False),
    (re.compile(r"creativecommons\.org/licenses/by/4\.0"), "CC-BY-4.0", False),
    (re.compile(r"creativecommons\.org/licenses/by/3\.0"), "CC-BY-3.0", False),
    (re.compile(r"creativecommons\.org/publicdomain/zero/1\.0"), "CC0-1.0", False),
)
# FreeDict headers commonly link the *unversioned* gnu.org page (e.g.
# ".../licenses/gpl.html") and name the version only in the link's own text
# ("... version 3.0 or any later version"). (URL pattern, SPDX family.)
_UNVERSIONED_FAMILY_PATTERNS: Sequence[tuple[re.Pattern[str], str]] = (
    (re.compile(r"gnu\.org/(licenses|copyleft)/agpl\.html"), "AGPL"),
    (re.compile(r"gnu\.org/(licenses|copyleft)/lgpl\.html"), "LGPL"),
    (re.compile(r"gnu\.org/(licenses|copyleft)/gpl\.html"), "GPL"),
    (re.compile(r"gnu\.org/(licenses|copyleft)/fdl\.html"), "GFDL"),
)
_OR_LATER_RE = re.compile(r"or later|any later version|and later", re.IGNORECASE)
_VERSION_NUMBER_RE = re.compile(r"\bver(?:sion)?\.?\s*(\d)(?:\.\d+)?", re.IGNORECASE)
# A Creative Commons licence stated as plain text, with no `<ref>` at all
# (seen in a handful of older headers, e.g. "Creative Commons Attribution 3.0
# Unported License").
_CC_TEXT_RE = re.compile(
    r"creative commons attribution(?P<sa>[\s-]+sharealike)?\s+(?P<ver>\d+(?:\.\d+)?)",
    re.IGNORECASE,
)


def _find_version_number(text: str) -> str | None:
    match = _VERSION_NUMBER_RE.search(text)
    return match.group(1) if match else None


def detect_license(
    availability_text: str, refs: Sequence[tuple[str, str]]
) -> tuple[str, str] | None:
    """A licence recognised in `availability_text`/`refs` (each ref's `target`
    and its own visible text, in document order), mapped to an SPDX id and the
    URL that justifies it; `None` when nothing is recognised (never guessed).

    Tries, in order: a URL that already names a version; an unversioned GNU
    URL whose link text or the surrounding text names one; a Creative Commons
    licence named in plain text with no link at all. A GPL-family id (GPL,
    AGPL, LGPL, GFDL) gets "-or-later" when the text says so, else "-only".
    """
    for target, _text in refs:
        for pattern, base, has_variant in _LICENSE_URL_PATTERNS:
            if pattern.search(target):
                if not has_variant:
                    return base, target
                suffix = "-or-later" if _OR_LATER_RE.search(availability_text) else "-only"
                return f"{base}{suffix}", target
    for target, text in refs:
        for pattern, family in _UNVERSIONED_FAMILY_PATTERNS:
            if pattern.search(target):
                version = _find_version_number(text) or _find_version_number(availability_text)
                if version is None:
                    continue
                suffix = (
                    "-or-later"
                    if _OR_LATER_RE.search(text) or _OR_LATER_RE.search(availability_text)
                    else "-only"
                )
                return f"{family}-{version}.0{suffix}", target
    match = _CC_TEXT_RE.search(availability_text)
    if match:
        version = match.group("ver")
        base = "CC-BY-SA" if match.group("sa") else "CC-BY"
        kind = "by-sa" if match.group("sa") else "by"
        return f"{base}-{version}", f"https://creativecommons.org/licenses/{kind}/{version}/"
    return None


# ---------------------------------------------------------------------------
# TEI header parsing
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class HeaderInfo:
    title: str
    authors: str
    license_spdx: str | None
    license_url: str | None

    def to_json(self) -> dict[str, object]:
        return {
            "title": self.title,
            "authors": self.authors,
            "license": self.license_spdx,
            "license_url": self.license_url,
        }

    @staticmethod
    def from_json(obj: Json) -> HeaderInfo:
        license_spdx = obj.get("license")
        license_url = obj.get("license_url")
        return HeaderInfo(
            title=str(obj.get("title", "")),
            authors=str(obj.get("authors", "")),
            license_spdx=str(license_spdx) if isinstance(license_spdx, str) else None,
            license_url=str(license_url) if isinstance(license_url, str) else None,
        )


def _clean_text(el: Element) -> str:
    return _WS_RE.sub(" ", "".join(el.itertext())).strip()


def _tag(name: str) -> str:
    return f"{{{TEI_NS}}}{name}"


def parse_header_fragment(raw: bytes) -> HeaderInfo:
    """A `HeaderInfo` from a byte fragment containing a `<teiHeader>...</teiHeader>`
    element (the default namespace need not be declared on it)."""
    text = raw.decode("utf-8", errors="replace")
    start = text.find("<teiHeader")
    end = text.find("</teiHeader>")
    if start == -1 or end == -1:
        raise FreeDictError("teiHeader not found (or not closed) within the header peek cap")
    end += len("</teiHeader>")
    snippet = text[start:end]
    tag_end = snippet.index(">")
    if "xmlns=" not in snippet[:tag_end]:
        snippet = snippet.replace("<teiHeader", f'<teiHeader xmlns="{TEI_NS}"', 1)
    try:
        root = ET.fromstring(snippet)
    except ET.ParseError as exc:
        raise FreeDictError(f"invalid teiHeader XML: {exc}") from exc

    title_el = root.find(f".//{_tag('title')}")
    title = _clean_text(title_el) if title_el is not None else ""

    names: list[str] = []
    for wanted in ("author", "editor"):
        for el in root.findall(f".//{_tag(wanted)}"):
            text_value = _clean_text(el)
            if text_value and text_value not in names:
                names.append(text_value)
    for resp in root.findall(f".//{_tag('respStmt')}"):
        name_el = resp.find(_tag("name"))
        if name_el is not None:
            text_value = _clean_text(name_el)
            if text_value and text_value != "[up for grabs]" and text_value not in names:
                names.append(text_value)
    authors = "; ".join(names)

    license_spdx: str | None = None
    license_url: str | None = None
    availability = root.find(f".//{_tag('availability')}")
    if availability is not None:
        avail_text = "".join(availability.itertext())
        refs = [
            (el.get("target", ""), _clean_text(el))
            for el in availability.iter()
            if el.tag in (_tag("ref"), _tag("ptr"))
        ]
        result = detect_license(avail_text, [(t, txt) for t, txt in refs if t])
        if result is not None:
            license_spdx, license_url = result

    return HeaderInfo(
        title=title, authors=authors, license_spdx=license_spdx, license_url=license_url
    )


def _read_until_header_close(fileobj: IO[bytes], cap: int = _HEADER_PEEK_CAP) -> bytes:
    buffer = bytearray()
    marker = b"</teiHeader>"
    while len(buffer) < cap:
        chunk = fileobj.read(1 << 14)
        if not chunk:
            break
        buffer.extend(chunk)
        if marker in buffer:
            break
    return bytes(buffer)


def _extract_header(tar: tarfile.TarFile) -> HeaderInfo:
    for member in tar:
        if member.name.endswith(".tei"):
            fileobj = tar.extractfile(member)
            if fileobj is None:
                raise FreeDictError(f"{member.name}: has no content")
            return parse_header_fragment(_read_until_header_close(fileobj))
    raise FreeDictError("no .tei member found in archive")


def peek_header_from_url(url: str, *, timeout: float = 30.0) -> HeaderInfo:
    """The header of the `.tei` member of the tarball at `url`, reading only as
    much of the (streamed, still-compressed) response as the header needs."""
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with (
        urllib.request.urlopen(request, timeout=timeout) as response,
        tarfile.open(fileobj=response, mode="r|xz") as tar,
    ):
        return _extract_header(tar)


def read_header_from_release(tarball_path: Path) -> HeaderInfo:
    with tarfile.open(tarball_path, mode="r:xz") as tar:
        return _extract_header(tar)


def _header_cache_path(cache_dir: Path) -> Path:
    return cache_dir / "header-cache.json"


def load_header_cache(cache_dir: Path) -> dict[str, HeaderInfo]:
    path = _header_cache_path(cache_dir)
    if not path.exists():
        return {}
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}
    if not isinstance(raw, Mapping):
        return {}
    return {
        key: HeaderInfo.from_json(value) for key, value in raw.items() if isinstance(value, Mapping)
    }


def save_header_cache(cache_dir: Path, cache: Mapping[str, HeaderInfo]) -> None:
    path = _header_cache_path(cache_dir)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {key: header.to_json() for key, header in cache.items()}
    path.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8"
    )


# ---------------------------------------------------------------------------
# Eligibility (DOCS/sources.md "FreeDict notes"): headword count + a licence
# `detect_license` recognises. Never applied without a documented reason.
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Eligibility:
    entry: DatabaseEntry
    header: HeaderInfo | None
    reason: str | None  # None means eligible

    @property
    def eligible(self) -> bool:
        return self.reason is None


def _cache_key(entry: DatabaseEntry) -> str:
    return f"{entry.name}@{entry.edition}"


def _probe_header(entry: DatabaseEntry, cache_dir: Path, timeout: float) -> HeaderInfo:
    release = release_path(cache_dir, entry)
    if release.exists():
        return read_header_from_release(release)
    return peek_header_from_url(entry.src_url, timeout=timeout)


def list_eligible(
    cache_dir: Path,
    entries: Sequence[DatabaseEntry],
    *,
    timeout: float = 20.0,
    max_workers: int = 12,
) -> tuple[list[tuple[DatabaseEntry, HeaderInfo]], Counter[str]]:
    """Every eligible dictionary with its header, and a count of exclusions by reason.

    Headers are read from `<cache>/header-cache.json` when the entry's edition
    is already cached, from the local tarball when one has been fetched, else
    peeked from the network (in parallel: this is one HTTP request per
    dictionary not yet known).
    """
    cache = load_header_cache(cache_dir)
    reasons: Counter[str] = Counter()
    candidates = [e for e in entries if e.headwords >= _MIN_HEADWORDS]
    reasons[_REASON_TOO_FEW] = len(entries) - len(candidates)

    def probe(entry: DatabaseEntry) -> tuple[DatabaseEntry, HeaderInfo | None, str | None]:
        key = _cache_key(entry)
        cached = cache.get(key)
        if cached is not None:
            header = cached
        else:
            try:
                header = _probe_header(entry, cache_dir, timeout)
            except (FreeDictError, OSError, ValueError) as exc:
                logger.warning("%s: %s", entry.name, exc)
                return entry, None, _REASON_FETCH_ERROR
        reason = None if (header.license_spdx and header.license_url) else _REASON_NO_LICENCE
        return entry, header, reason

    eligible: list[tuple[DatabaseEntry, HeaderInfo]] = []
    with ThreadPoolExecutor(max_workers=max_workers) as pool:
        for entry, header, reason in pool.map(probe, candidates):
            if header is not None:
                cache[_cache_key(entry)] = header
            if reason is None:
                assert header is not None
                eligible.append((entry, header))
            else:
                reasons[reason] += 1

    save_header_cache(cache_dir, cache)
    eligible.sort(key=lambda pair: dict_id_for(pair[0]))
    return eligible, reasons


# ---------------------------------------------------------------------------
# DictSpec construction
# ---------------------------------------------------------------------------

_DEFAULT_PUBLISHER = "FreeDict contributors"


def make_spec(entry: DatabaseEntry, header: HeaderInfo) -> DictSpec:
    if header.license_spdx is None or header.license_url is None:
        raise FreeDictError(f"{entry.name}: no recognised licence, cannot build a DictSpec")
    src_code = lang_code(entry.src_lang3)
    tgt_code = lang_code(entry.tgt_lang3)
    publisher = header.authors or _DEFAULT_PUBLISHER
    title = (
        header.title
        or f"{lang_name(entry.src_lang3)}-{lang_name(entry.tgt_lang3)} FreeDict Dictionary"
    )
    attribution = f"{title}, {publisher}, {header.license_spdx}"
    return DictSpec(
        dict_id=f"freedict-{src_code}-{tgt_code}",
        name=f"{lang_name(entry.src_lang3)}-{lang_name(entry.tgt_lang3)} (FreeDict)",
        source_lang=src_code,
        target_lang=tgt_code,
        version=version_from_edition(entry.edition),
        publisher=publisher,
        license=header.license_spdx,
        license_url=header.license_url,
        attribution=attribution,
        kind="bilingual",
    )


# ---------------------------------------------------------------------------
# Fetching a dictionary's tarball
# ---------------------------------------------------------------------------


def release_dir(cache_dir: Path, name: str) -> Path:
    return cache_dir / name


def release_path(cache_dir: Path, entry: DatabaseEntry) -> Path:
    return release_dir(cache_dir, entry.name) / f"freedict-{entry.name}-{entry.edition}.src.tar.xz"


def fetch_release(cache_dir: Path, entry: DatabaseEntry, *, timeout: float = 300.0) -> Path:
    """Download and sha512-verify `entry`'s tarball into `cache_dir`, unless a copy
    for this exact edition is already there. Stale tarballs of older editions of
    the same dictionary are removed."""
    target = release_path(cache_dir, entry)
    if target.exists():
        return target
    directory = release_dir(cache_dir, entry.name)
    directory.mkdir(parents=True, exist_ok=True)
    for stale in directory.glob("*.src.tar.xz"):
        stale.unlink()

    request = urllib.request.Request(entry.src_url, headers={"User-Agent": USER_AGENT})
    partial = target.with_name(target.name + ".part")
    hasher = hashlib.sha512()
    try:
        with (
            urllib.request.urlopen(request, timeout=timeout) as response,
            partial.open("wb") as out,
        ):
            while True:
                chunk = response.read(1 << 20)
                if not chunk:
                    break
                hasher.update(chunk)
                out.write(chunk)
    except urllib.error.HTTPError as exc:
        partial.unlink(missing_ok=True)
        raise FreeDictError(f"cannot fetch {entry.src_url}: HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        partial.unlink(missing_ok=True)
        raise FreeDictError(f"cannot fetch {entry.src_url}: {exc.reason}") from exc

    digest = hasher.hexdigest()
    if digest != entry.src_sha512:
        partial.unlink(missing_ok=True)
        raise FreeDictError(
            f"{entry.name}: checksum mismatch (expected {entry.src_sha512}, got {digest})"
        )
    partial.replace(target)
    logger.info("fetched %s (sha512 verified)", target)
    return target


# ---------------------------------------------------------------------------
# TEI entry -> canonical Entry
# ---------------------------------------------------------------------------


def _normalize_pos_token(raw: str) -> str:
    token = raw.strip().split(":", 1)[0].split("/", 1)[0]
    return token.rstrip(".").strip().lower()


_POS_MAP: Mapping[str, str] = {
    "n": "noun",
    "noun": "noun",
    "propn": "noun",
    "name": "noun",
    "v": "verb",
    "vi": "verb",
    "vt": "verb",
    "vti": "verb",
    "verb": "verb",
    "auxv": "verb",
    "mv": "verb",
    "vneg": "verb",
    "vp": "verb",
    "adj": "adj",
    "adjective": "adj",
    "adv": "adv",
    "adverb": "adv",
    "pron": "pron",
    "pronoun": "pron",
    "refl pron": "pron",
    "rel pron": "pron",
    "interro": "pron",
    "prep": "prep",
    "preposition": "prep",
    "conj": "conj",
    "conjunction": "conj",
    "interj": "interj",
    "int": "interj",
    "interjection": "interj",
    "det": "det",
    "article": "det",
    "art": "det",
    "determiner": "det",
    "num": "num",
    "numeral": "num",
    "number": "num",
    "particle": "particle",
    "ptcl": "particle",
    "part": "particle",
    "prefix": "prefix",
    "pref": "prefix",
    "suffix": "suffix",
    "phrase": "phrase",
    "idm": "phrase",
    "phrv": "phrase",
    "phrvi": "phrase",
    "phrvt": "phrase",
    "comb form": "phrase",
    "abbr": "abbr",
    "abbreviation": "abbr",
}


def map_pos(raw: str, unknown: Counter[str] | None = None) -> str:
    pos = _POS_MAP.get(_normalize_pos_token(raw), "other")
    if pos == "other" and unknown is not None:
        unknown[raw.strip()] += 1
    assert pos in POS_TAGS
    return pos


_GEN_MAP: Mapping[str, str] = {
    "masc": "masculine",
    "m": "masculine",
    "fem": "feminine",
    "f": "feminine",
    "neut": "neuter",
    "n": "neuter",
}


def _gen_pattern(gen_raw: str | None) -> str | None:
    if not gen_raw:
        return None
    key = gen_raw.strip().lower()
    return _GEN_MAP.get(key, gen_raw.strip())


# IPA-only or IPA-favoured symbols: enough to tell a broad phonetic
# transcription (FreeDict's usual `<pron>` content) from something that is
# clearly not IPA. FreeDict has no separate plain-transcription field, so a
# `<pron>` that does not look like IPA is dropped rather than mislabelled.
_IPA_HINT_CHARS = "ˈˌːʃʒθðŋæɑɒɔəɛɜɪɵʊʌɡʔ"


def _looks_like_ipa(text: str) -> bool:
    stripped = text.strip()
    if not stripped:
        return False
    if stripped[0] in "/[":
        return True
    return any(ch in _IPA_HINT_CHARS for ch in stripped)


def _esc(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def _walk_forms(form_el: Element, depth: int = 0) -> list[tuple[str, str | None]]:
    """(text, type) for `form_el`'s own `<orth>` and every nested `<form>`'s,
    depth-first, in document order. `type` is `form_el`'s own `@type`
    (`None` for the outermost form, the headword)."""
    results: list[tuple[str, str | None]] = []
    orth_el = form_el.find(_tag("orth"))
    text = _clean_text(orth_el) if orth_el is not None else ""
    if text:
        results.append((text, form_el.get("type") if depth > 0 else None))
    for nested in form_el.findall(_tag("form")):
        results.extend(_walk_forms(nested, depth + 1))
    return results


def _entry_gram_grp(entry_el: Element) -> tuple[str | None, str | None]:
    pos_raw: str | None = None
    gen_raw: str | None = None
    for gram in entry_el.findall(_tag("gramGrp")):
        if pos_raw is None:
            pos_el = gram.find(_tag("pos"))
            if pos_el is not None:
                pos_raw = _clean_text(pos_el)
        if gen_raw is None:
            gen_el = gram.find(_tag("gen"))
            if gen_el is not None:
                gen_raw = _clean_text(gen_el)
    return pos_raw or None, gen_raw or None


def _translations(sense_el: Element) -> list[str]:
    out: list[str] = []
    for cit in sense_el.findall(_tag("cit")):
        if cit.get("type") != "trans":
            continue
        quote = cit.find(_tag("quote"))
        if quote is None:
            continue
        text = _clean_text(quote)
        if text and text not in out:
            out.append(text)
    return out


def _def_text(sense_el: Element) -> str | None:
    def_el = sense_el.find(_tag("def"))
    if def_el is None:
        return None
    text = _clean_text(def_el)
    return text or None


def _usages(sense_el: Element) -> list[str]:
    out: list[str] = []
    for usg in sense_el.findall(_tag("usg")):
        text = _clean_text(usg)
        if text and text not in out:
            out.append(text)
    return out


def _examples(sense_el: Element) -> list[Example]:
    examples: list[Example] = []
    for cit in sense_el.findall(_tag("cit")):
        if cit.get("type") != "example":
            continue
        quote = cit.find(_tag("quote"))
        if quote is None:
            continue
        text = _clean_text(quote)
        if not text:
            continue
        translation: str | None = None
        for nested in cit.findall(_tag("cit")):
            if nested.get("type") != "trans":
                continue
            nested_quote = nested.find(_tag("quote"))
            if nested_quote is not None:
                nested_text = _clean_text(nested_quote)
                if nested_text:
                    translation = nested_text
                    break
        examples.append(
            Example(text=_esc(text), translation=_esc(translation) if translation else None)
        )
        if len(examples) >= _MAX_EXAMPLES:
            break
    return examples


_XR_TYPE_MAP: Mapping[str, str] = {"syn": "synonym", "ant": "antonym", "see": "see"}


def _xr_relations(container_el: Element, sense_ordinal: int | None) -> list[Relation]:
    relations: list[Relation] = []
    for xr in container_el.findall(_tag("xr")):
        rel_type = _XR_TYPE_MAP.get(xr.get("type", ""), "see")
        for ref in xr.findall(_tag("ref")):
            text = _clean_text(ref)
            if text:
                relations.append(Relation(type=rel_type, target=text, sense_ordinal=sense_ordinal))
    return relations


@dataclass(slots=True)
class ConversionStats:
    records: int = 0
    entries: int = 0
    skipped_other: int = 0
    unknown_pos: Counter[str] = field(default_factory=Counter)


@dataclass(slots=True)
class _ParsedEntry:
    headword: str
    forms: list[Form]
    pronunciation: Pronunciation | None
    senses: list[Sense]
    relations: list[Relation]


def parse_tei_entry(entry_el: Element, stats: ConversionStats) -> _ParsedEntry | None:
    """One TEI `<entry>` (pre-merge) as headword, forms, pronunciation, senses
    and relations, or `None` when it has no headword or no usable sense."""
    form_el = entry_el.find(_tag("form"))
    if form_el is None:
        return None
    walked = _walk_forms(form_el)
    if not walked:
        return None
    headword = walked[0][0]
    forms = [Form(form=text, tag=(kind or "variant")) for text, kind in walked[1:]]

    pron_el = form_el.find(f".//{_tag('pron')}")
    pronunciation: Pronunciation | None = None
    if pron_el is not None:
        pron_text = _clean_text(pron_el)
        if pron_text and _looks_like_ipa(pron_text):
            pronunciation = Pronunciation(ipa=pron_text)

    pos_raw, gen_raw = _entry_gram_grp(entry_el)
    pos = map_pos(pos_raw, stats.unknown_pos) if pos_raw else None
    pattern = _gen_pattern(gen_raw)

    sense_els = entry_el.findall(_tag("sense"))
    sense_sources = sense_els if sense_els else [entry_el]

    senses: list[Sense] = []
    relations: list[Relation] = []
    for sense_el in sense_sources:
        translations = _translations(sense_el)
        def_text = _def_text(sense_el)
        parts: list[str] = []
        if translations:
            parts.append(", ".join(translations))
        if def_text:
            parts.append(f"({def_text})" if parts else def_text)
        definition = " ".join(parts).strip()
        if not definition:
            continue
        usages = _usages(sense_el)
        senses.append(
            Sense(
                definition=_esc(definition),
                pos=pos,
                pattern=pattern,
                label=", ".join(usages) if usages else None,
                examples=tuple(_examples(sense_el)),
            )
        )
        relations.extend(_xr_relations(sense_el, len(senses)))
    if sense_els:
        # Defensive: some dictionaries may place `<xr>` directly under `<entry>`
        # rather than inside a `<sense>`. Not seen in the corpus checked, but
        # cheap to handle rather than silently drop.
        relations.extend(_xr_relations(entry_el, None))

    if not senses:
        stats.skipped_other += 1
        return None
    return _ParsedEntry(headword, forms, pronunciation, senses, relations)


@dataclass(slots=True)
class _Accumulator:
    """The entry being assembled from consecutive `<entry>` elements for one headword."""

    headword: str
    lang: str
    senses: list[Sense] = field(default_factory=list)
    pronunciations: list[Pronunciation] = field(default_factory=list)
    forms: list[Form] = field(default_factory=list)
    relations: list[Relation] = field(default_factory=list)

    def add(self, parsed: _ParsedEntry) -> None:
        base_ordinal = len(self.senses)
        self.senses.extend(parsed.senses)
        for relation in parsed.relations:
            if relation.sense_ordinal is not None:
                self.relations.append(
                    Relation(
                        type=relation.type,
                        target=relation.target,
                        sense_ordinal=relation.sense_ordinal + base_ordinal,
                    )
                )
            else:
                self.relations.append(relation)
        if parsed.pronunciation is not None and parsed.pronunciation not in self.pronunciations:
            self.pronunciations.append(parsed.pronunciation)
        known = {f.form for f in self.forms}
        for form in parsed.forms:
            if form.form not in known and form.form != self.headword:
                self.forms.append(form)
                known.add(form.form)

    def entry(self) -> Entry | None:
        if not self.senses:
            return None
        return Entry(
            headword=self.headword,
            lang=self.lang,
            senses=tuple(self.senses),
            pronunciations=tuple(self.pronunciations),
            forms=tuple(self.forms),
            relations=tuple(self.relations),
        )


def iter_tei_entries(fileobj: IO[bytes]) -> Iterator[Element]:
    """`<entry>` elements of a TEI body, streamed: memory stays bounded no
    matter how large the dictionary, by clearing each entry (and the root's
    already-visited children) once it has been yielded."""
    events = ET.iterparse(fileobj, events=("start", "end"))
    try:
        _, root = next(events)
    except StopIteration:
        return
    entry_tag = _tag("entry")
    for event, elem in events:
        if event == "end" and elem.tag == entry_tag:
            yield elem
            elem.clear()
            root.clear()


def convert_tei_entries(
    entries: Iterable[Element], lang: str, stats: ConversionStats
) -> Iterator[Entry]:
    """Canonical entries from TEI `<entry>` elements of one dictionary, merging
    consecutive elements that share a headword (PLAN 6.4, same rule as Kaikki)."""
    current: _Accumulator | None = None
    for entry_el in entries:
        stats.records += 1
        parsed = parse_tei_entry(entry_el, stats)
        if parsed is None:
            continue
        if current is None or current.headword != parsed.headword:
            if current is not None and (done := current.entry()) is not None:
                stats.entries += 1
                yield done
            current = _Accumulator(headword=parsed.headword, lang=lang)
        current.add(parsed)
    if current is not None and (done := current.entry()) is not None:
        stats.entries += 1
        yield done


def convert_tei_file(
    tarball_path: Path, entry: DatabaseEntry, stats: ConversionStats
) -> Iterator[Entry]:
    lang = lang_code(entry.src_lang3)
    with tarfile.open(tarball_path, mode="r:xz") as tar:
        for member in tar:
            if member.name.endswith(".tei"):
                fileobj = tar.extractfile(member)
                if fileobj is None:
                    raise FreeDictError(f"{member.name}: has no content")
                yield from convert_tei_entries(iter_tei_entries(fileobj), lang, stats)
                return
    raise FreeDictError(f"{tarball_path}: no .tei member found")


# ---------------------------------------------------------------------------
# Converter contract
# ---------------------------------------------------------------------------


class FreeDictConverter(Converter):
    """The `Converter` contract over FreeDict's database index and cached tarballs."""

    source_id: ClassVar[str] = "freedict"

    def __init__(self, cache_dir: Path) -> None:
        self.cache_dir = cache_dir
        self.stats = ConversionStats()

    def fetch(self, cache_dir: Path) -> Path:
        """Refreshes the database index only. Unlike Kaikki's fixed 3-language
        list, FreeDict lists ~300 dictionaries; downloading every tarball here
        would need several GB, so a specific dictionary's tarball is fetched
        on demand by `fetch_release` (the CLI does this for `--dict`)."""
        fetch_database(cache_dir)
        return cache_dir

    def dictionaries(self) -> list[DictSpec]:
        specs: list[DictSpec] = []
        for entry in read_database(database_path(self.cache_dir)):
            release = release_path(self.cache_dir, entry)
            if not release.exists():
                continue
            try:
                header = read_header_from_release(release)
            except FreeDictError:
                continue
            if header.license_spdx is None or header.license_url is None:
                continue
            specs.append(make_spec(entry, header))
        return specs

    def iter_records(self, spec: DictSpec) -> Iterator[Entry]:
        entries = read_database(database_path(self.cache_dir))
        entry = find_database_entry(entries, spec.dict_id)
        release = release_path(self.cache_dir, entry)
        if not release.exists():
            raise FreeDictError(f"{spec.dict_id}: tarball not fetched, run with --dict first")
        return convert_tei_file(release, entry, self.stats)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _print_list(
    eligible: Sequence[tuple[DatabaseEntry, HeaderInfo]], *, markdown: bool, ids_only: bool
) -> None:
    if markdown:
        print("| dict_id | dictionary | headwords | licence |")
        print("|---|---|---|---|")
        for entry, header in eligible:
            name = f"{lang_name(entry.src_lang3)}-{lang_name(entry.tgt_lang3)}"
            print(f"| {dict_id_for(entry)} | {name} | {entry.headwords} | {header.license_spdx} |")
        return
    if ids_only:
        for entry, _header in eligible:
            print(dict_id_for(entry))
        return
    for entry, header in eligible:
        name = f"{lang_name(entry.src_lang3)}-{lang_name(entry.tgt_lang3)}"
        print(f"{dict_id_for(entry)}\t{name}\t{entry.headwords}\t{header.license_spdx}")


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.converters.freedict")
    parser.add_argument("--dict", dest="dict_id", help="dictionary to build, e.g. freedict-de-en")
    parser.add_argument(
        "--out", type=Path, default=Path("out"), help="bundles go to OUT/<dict_id>/"
    )
    parser.add_argument("--cache", type=Path, default=Path("sources/freedict"))
    parser.add_argument(
        "--no-fetch", action="store_true", help="use the cached database/tarball as is"
    )
    parser.add_argument(
        "--fetch-only", action="store_true", help="fetch the tarball, build nothing"
    )
    parser.add_argument("--limit", type=int, default=None, help="stop after N entries (testing)")
    parser.add_argument("--built-at", default=None, help="fixed build time, YYYY-MM-DDTHH:MM:SSZ")
    parser.add_argument(
        "--no-vacuum",
        action="store_true",
        help="skip the final VACUUM (it needs a temporary copy of the whole bundle)",
    )
    parser.add_argument("--timeout", type=float, default=20.0, help="per-request network timeout")
    parser.add_argument(
        "--list", action="store_true", help="list the eligible dictionaries and exit"
    )
    parser.add_argument("--ids-only", action="store_true", help="with --list, print only dict_ids")
    parser.add_argument(
        "--markdown", action="store_true", help="with --list, print a markdown table"
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(sys.argv[1:] if argv is None else argv)

    if args.list:
        try:
            if not args.no_fetch or not database_path(args.cache).exists():
                fetch_database(args.cache, timeout=args.timeout)
            entries = read_database(database_path(args.cache))
            eligible, reasons = list_eligible(args.cache, entries, timeout=args.timeout)
        except FreeDictError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1
        _print_list(eligible, markdown=args.markdown, ids_only=args.ids_only)
        if not args.markdown and not args.ids_only:
            total_excluded = sum(reasons.values())
            print(
                f"eligible: {len(eligible)}; excluded: {total_excluded} {dict(reasons)}",
                file=sys.stderr,
            )
        return 0

    if not args.dict_id:
        print("error: --dict is required (see --list)", file=sys.stderr)
        return 1

    try:
        if not args.no_fetch or not database_path(args.cache).exists():
            fetch_database(args.cache, timeout=args.timeout)
        entries = read_database(database_path(args.cache))
        db_entry = find_database_entry(entries, args.dict_id)
        release = release_path(args.cache, db_entry)
        if not (args.no_fetch and release.exists()):
            fetch_release(args.cache, db_entry, timeout=max(args.timeout, 300.0))
        if args.fetch_only:
            return 0

        header = read_header_from_release(release)
        if header.license_spdx is None or header.license_url is None:
            print(
                f"error: {args.dict_id}: no recognised free licence in its TEI header",
                file=sys.stderr,
            )
            return 1
        spec = make_spec(db_entry, header)

        converter = FreeDictConverter(args.cache)
        record_iter: Iterable[Entry] = converter.iter_records(spec)
        if args.limit is not None:
            record_iter = islice(record_iter, args.limit)
        built_at = (
            datetime.strptime(args.built_at, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)
            if args.built_at
            else None
        )
        out_path = build_bundle(
            record_iter,
            spec,
            args.out / spec.dict_id,
            built_at=built_at,
            extra_meta={
                "source_converter": FreeDictConverter.source_id,
                "source_edition": db_entry.edition,
            },
            vacuum=not args.no_vacuum,
        )
    except (FreeDictError, BuildError, SchemaError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    stats = converter.stats
    print(
        f"built {out_path}: {stats.entries} entries from {stats.records} records "
        f"({stats.skipped_other} skipped)"
    )
    if stats.unknown_pos:
        print(f"parts of speech mapped to 'other': {dict(stats.unknown_pos.most_common())}")
    return 0


__all__ = [
    "FREEDICT_DATABASE_URL",
    "ConversionStats",
    "DatabaseEntry",
    "DatabaseInfo",
    "Eligibility",
    "FreeDictConverter",
    "FreeDictError",
    "HeaderInfo",
    "convert_tei_entries",
    "detect_license",
    "dict_id_for",
    "fetch_database",
    "fetch_release",
    "find_database_entry",
    "lang_code",
    "lang_name",
    "list_eligible",
    "make_spec",
    "map_pos",
    "parse_database",
    "parse_header_fragment",
    "parse_tei_entry",
    "read_database",
    "version_from_edition",
]

if __name__ == "__main__":
    raise SystemExit(main())
