"""JMdict (Japanese-English), from the Electronic Dictionary Research and
Development Group (EDRDG), PLAN.md 6.2.

JMdict is a single gzipped XML file (`JMdict_e.gz`, English glosses only)
with an internal DTD that declares one entity per part-of-speech, usage,
field and dialect code (`&n;`, `&v5k;`, `&uk;`, ...). There is exactly one
dictionary here, `jmdict-ja-en`.

Entities (DECISION, see DOCS/sources.md "JMdict notes" for the long version):
`xml.etree.ElementTree.iterparse` is fed the whole file, and the internal
DTD subset is part of that same stream, so `expat` expands every entity
reference to its declared value automatically (`<pos>&n;</pos>` parses as
text "noun (common) (futsuumeishi)") -- there is no way to make it keep the
short code instead. Losing the code would break part-of-speech mapping, so
`_parse_header` separately reads just the header bytes (before `<JMdict>`)
and regex-extracts every `<!ENTITY code "value">` declaration into a
`value -> code` table; because every entity's expansion text is unique,
looking up a resolved `<pos>`/`<misc>`/`<field>`/`<dial>` element's `.text`
in that table recovers the original code. The same header read also pulls
the "JMdict created: YYYY-MM-DD" comment used for `bundle_version`.

Mapping decisions (DOCS/sources.md):
- headword is the first `<keb>` (kanji form), or the first `<reb>` (reading)
  when the entry has no kanji form;
- the remaining kebs become `forms` tagged "variant", every reb becomes a
  form tagged "reading", and every reb is additionally romanized (PyICU
  `Any-Latin; Latin-ASCII`, which covers hiragana, katakana and the chouon
  mark in one pass) as a form tagged "romanization", so Latin input such as
  "taberu" finds an entry after headword normalization;
- each `<sense>`'s glosses join with "; " as the definition; a sense with no
  gloss text is dropped;
- `<pos>` maps to the schema POS set by prefix (`n*` -> noun, `v*` -> verb,
  `adj-*` -> adj, `adv*` -> adv, `pn` -> pron, `prt` -> particle, `conj` ->
  conj, `int` -> interj, `num` -> num); everything else (`ctr`, `suf`,
  `pref`, `n-pref`, `n-suf`, `exp`, `aux*`, `cop`, `unc`, ...) maps to
  "other" and is counted. When a sense carries several `<pos>` tags the
  first one that maps to something other than "other" wins; `vt`/`vi` among
  them additionally set the sense `pattern` to "transitive"/"intransitive";
- `<misc>`, `<field>` and `<dial>` become the sense `label`: most entity
  values are already short ("colloquial", "computing", "Kansai-ben"), a
  small override table shortens the few long ones ("word usually written
  using kana alone" -> "usually kana"); `<s_inf>` text joins the same label;
- `<xref>`/`<ant>` become "see"/"antonym" relations on the sense; JMdict
  joins a target keb/reb/sense-number with a centre dot ("丸・まる・1"), so
  only the first, centre-dot-free segment (always the headword) is kept as
  the relation target;
- `ke_pri`/`re_pri` priority codes (news1/2, ichi1/2, spec1/2, gai1/2,
  nf01-48) become the entry `frequency`: news1/ichi1/spec1/gai1 -> 5,
  news2/ichi2/spec2/gai2 -> 4, a standalone `nf01`-`nf24` -> 3, a standalone
  `nf25`-`nf48` -> 2 (in real data `nf` always accompanies a `news` tag, so
  this branch is a fallback), any other priority code -> 1, no priority
  code at all -> no frequency.

Run: `python -m omnipipe.converters.jmdict --dict jmdict-ja-en --out out/`
"""

from __future__ import annotations

import argparse
import gzip
import html
import logging
import re
import shutil
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from collections import Counter
from collections.abc import Iterable, Iterator, Sequence
from dataclasses import dataclass, field
from datetime import UTC, date, datetime
from itertools import islice
from pathlib import Path
from typing import ClassVar

import icu

from omnipipe.build import BuildError, build_bundle
from omnipipe.converters.base import Converter
from omnipipe.schema import (
    POS_TAGS,
    DictSpec,
    Entry,
    Form,
    Relation,
    SchemaError,
    Sense,
)

logger = logging.getLogger(__name__)

DICT_ID = "jmdict-ja-en"
JMDICT_URL = "http://ftp.edrdg.org/pub/Nihongo/JMdict_e.gz"
USER_AGENT = "omnipipe/0.1 (+https://github.com/keshavbhatt/omnidict)"

_NAME = "Japanese-English (JMdict)"
_PUBLISHER = "Electronic Dictionary Research and Development Group"
_LICENSE = "CC-BY-SA-4.0"
_LICENSE_URL = "https://creativecommons.org/licenses/by-sa/4.0/"
# Wording from https://www.edrdg.org/edrdg/sample.html, "For a Software Package",
# adapted from "files"/"These files are" to the single JMdict file this converter uses.
_ATTRIBUTION = (
    "This package uses the JMdict dictionary file. This file is the property of the "
    "Electronic Dictionary Research and Development Group, and is used in conformance "
    "with the Group's licence."
)

_HEADER_END_MARKER = b"<JMdict>"
_HEADER_READ_CAP = 1 << 20  # the DTD is a few dozen KB; this is a generous safety cap
_ENTITY_RE = re.compile(r'<!ENTITY\s+(\S+)\s+"([^"]*)">')
_CREATED_RE = re.compile(r"JMdict created:\s*(\d{4}-\d{2}-\d{2})")

_ROMAJI_TRANSFORM = "Any-Latin; Latin-ASCII"

# Sense tags shown as the grammar pattern (JMdict encodes transitivity as extra <pos> tags).
_TRANSITIVITY_PATTERN: dict[str, str] = {"vt": "transitive", "vi": "intransitive"}

# A handful of <misc> entity values are long parentheticals; shorten them for the label.
# Every other <misc>/<field>/<dial> code is already a short, readable word or two and is
# used as its entity value verbatim.
_LABEL_OVERRIDES: dict[str, str] = {
    "uk": "usually kana",
    "hon": "honorific",
    "hum": "humble",
    "pol": "polite",
    "X": "rude or X-rated",
}

# Priority tags (ke_pri/re_pri), see DOCS/sources.md "JMdict notes" for the mapping.
_TOP_PRIORITY = frozenset({"news1", "ichi1", "spec1", "gai1"})
_SECOND_PRIORITY = frozenset({"news2", "ichi2", "spec2", "gai2"})
_NF_RE = re.compile(r"^nf(\d{2})$")

_ENTRY_CLEAR_INTERVAL = 2000

type Elem = ET.Element


class JMdictError(Exception):
    """Raised when the dump cannot be fetched or does not parse as expected."""


# ---------------------------------------------------------------------------
# Fetching and the header (entities + creation date)
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class DumpInfo:
    """What is known about a fetched dump.

    Unlike Kaikki, JMdict needs no `<file>.json` sidecar: `created` comes from
    the dump's own "JMdict created:" comment and `fetched_at` from the file's
    mtime, so the downloaded file alone is enough to rebuild this.
    """

    url: str
    created: date
    fetched_at: str


def dump_path(cache_dir: Path) -> Path:
    return cache_dir / "JMdict_e.gz"


def fetch_dump(cache_dir: Path, *, timeout: float = 300.0) -> Path:
    """Download `JMdict_e.gz` into `cache_dir` unless the copy there is current."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    target = dump_path(cache_dir)
    headers = {"User-Agent": USER_AGENT}
    if target.exists():
        previous_mtime = target.stat().st_mtime
        headers["If-Modified-Since"] = datetime.fromtimestamp(previous_mtime, tz=UTC).strftime(
            "%a, %d %b %Y %H:%M:%S GMT"
        )

    partial = target.with_name(target.name + ".part")
    request = urllib.request.Request(JMDICT_URL, headers=headers)
    response_headers: dict[str, str] | None = None
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            response_headers = dict(response.headers)
            with partial.open("wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)
    except urllib.error.HTTPError as exc:
        if exc.code == 304 and target.exists():
            logger.info("%s is current", target)
            return target
        raise JMdictError(f"cannot fetch {JMDICT_URL}: HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise JMdictError(f"cannot fetch {JMDICT_URL}: {exc.reason}") from exc

    partial.replace(target)
    last_modified = response_headers.get("Last-Modified") if response_headers else None
    logger.info("fetched %s (%s)", target, last_modified)
    return target


def _read_header(dump: Path) -> str:
    """The file's text up to and including `<JMdict>`: the DTD plus the creation comment."""
    opener = gzip.open if dump.suffix == ".gz" else open
    chunks: list[bytes] = []
    total = 0
    with opener(dump, "rb") as handle:
        while True:
            chunk = handle.read(1 << 16)
            if not chunk:
                break
            chunks.append(chunk)
            total += len(chunk)
            joined = b"".join(chunks)
            if _HEADER_END_MARKER in joined:
                return joined.decode("utf-8", errors="replace")
            if total > _HEADER_READ_CAP:
                raise JMdictError(f"{dump}: no {_HEADER_END_MARKER!r} within the first {total}B")
    raise JMdictError(f"{dump}: end of file reached before {_HEADER_END_MARKER!r}")


def parse_entities(header: str) -> dict[str, str]:
    """`{expanded value: entity code}` for every `<!ENTITY code "value">` in `header`."""
    end = header.find("]>")
    dtd = header[:end] if end != -1 else header
    table: dict[str, str] = {}
    for match in _ENTITY_RE.finditer(dtd):
        code, value = match.group(1), match.group(2)
        if value in table and table[value] != code:
            logger.warning("entity value %r shared by %r and %r", value, table[value], code)
        table[value] = code
    return table


def parse_created_date(header: str) -> date:
    match = _CREATED_RE.search(header)
    if not match:
        raise JMdictError("could not find a 'JMdict created: YYYY-MM-DD' comment")
    return date.fromisoformat(match.group(1))


def read_dump_info(dump: Path) -> DumpInfo:
    header = _read_header(dump)
    created = parse_created_date(header)
    stat = dump.stat()
    return DumpInfo(
        url=JMDICT_URL,
        created=created,
        fetched_at=datetime.fromtimestamp(stat.st_mtime, tz=UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
    )


def bundle_version(created: date, build: int = 1) -> str:
    """`YYYY.MM.N`: the month JMdict says it was created, and a rebuild counter."""
    return f"{created.year}.{created.month:02d}.{build}"


def dictionary_spec(version: str) -> DictSpec:
    return DictSpec(
        dict_id=DICT_ID,
        name=_NAME,
        source_lang="ja",
        target_lang="en",
        version=version,
        publisher=_PUBLISHER,
        license=_LICENSE,
        license_url=_LICENSE_URL,
        attribution=_ATTRIBUTION,
        kind="bilingual",
    )


# ---------------------------------------------------------------------------
# Element helpers
# ---------------------------------------------------------------------------


def _texts(elem: Elem, tag: str) -> list[str]:
    return [(child.text or "").strip() for child in elem.findall(tag)]


def _coded(
    elem: Elem, tag: str, entities: dict[str, str], unresolved: Counter[str]
) -> list[tuple[str, str]]:
    """`(code, resolved value)` for every `<tag>` child, via the reverse entity table."""
    pairs: list[tuple[str, str]] = []
    for child in elem.findall(tag):
        value = (child.text or "").strip()
        code = entities.get(value)
        if code is None:
            unresolved[value] += 1
            code = value
        pairs.append((code, value))
    return pairs


def _esc(text: str) -> str:
    return html.escape(text, quote=False)


# ---------------------------------------------------------------------------
# POS, labels, relations, frequency
# ---------------------------------------------------------------------------


def map_pos(code: str, unknown: Counter[str] | None = None) -> str:
    """One JMdict `<pos>` entity code to the schema POS set (DOCS/sources.md)."""
    pos: str
    if code in ("n", "n-pr", "n-t", "n-adv"):
        pos = "noun"
    elif code.startswith("v"):
        pos = "verb"
    elif code.startswith("adj"):
        pos = "adj"
    elif code.startswith("adv"):
        pos = "adv"
    elif code == "pn":
        pos = "pron"
    elif code == "prt":
        pos = "particle"
    elif code == "conj":
        pos = "conj"
    elif code == "int":
        pos = "interj"
    elif code == "num":
        pos = "num"
    else:
        pos = "other"
    if pos == "other" and unknown is not None:
        unknown[code] += 1
    assert pos in POS_TAGS
    return pos


def _sense_pos(pos_codes: Sequence[str], unknown: Counter[str] | None) -> str | None:
    if not pos_codes:
        return None
    mapped = [map_pos(code, unknown) for code in pos_codes]
    for candidate in mapped:
        if candidate != "other":
            return candidate
    return "other"


def _sense_pattern(pos_codes: Sequence[str]) -> str | None:
    words = [_TRANSITIVITY_PATTERN[code] for code in pos_codes if code in _TRANSITIVITY_PATTERN]
    return ", ".join(words) if words else None


def _label_word(code: str, value: str) -> str:
    return _LABEL_OVERRIDES.get(code, value)


def _sense_label(*tag_groups: Sequence[tuple[str, str]], notes: Sequence[str]) -> str | None:
    words = [_label_word(code, value) for group in tag_groups for code, value in group]
    words.extend(note for note in notes if note)
    return ", ".join(words) if words else None


def _relation_target(raw: str) -> str:
    """The headword portion of a JMdict cross-reference ("丸・まる・1" -> "丸")."""
    return raw.split("・", 1)[0].strip()


def entry_frequency(priorities: Iterable[str]) -> int | None:
    tags = set(priorities)
    if not tags:
        return None
    if tags & _TOP_PRIORITY:
        return 5
    if tags & _SECOND_PRIORITY:
        return 4
    nf_numbers = [int(match.group(1)) for tag in tags if (match := _NF_RE.match(tag))]
    if nf_numbers:
        return 3 if min(nf_numbers) <= 24 else 2
    return 1


def _romanize(reading: str, transliterator: icu.Transliterator) -> str:
    return str(transliterator.transliterate(reading))


# ---------------------------------------------------------------------------
# Entry conversion
# ---------------------------------------------------------------------------


@dataclass(slots=True)
class ConversionStats:
    entries: int = 0
    skipped_no_senses: int = 0
    unknown_pos: Counter[str] = field(default_factory=Counter)
    unresolved_entities: Counter[str] = field(default_factory=Counter)


def convert_entry(
    entry_elem: Elem,
    entities: dict[str, str],
    transliterator: icu.Transliterator,
    stats: ConversionStats,
) -> Entry | None:
    kebs = [t for t in _texts(entry_elem, "k_ele/keb") if t]
    rebs = [t for t in _texts(entry_elem, "r_ele/reb") if t]
    if not kebs and not rebs:
        return None
    headword = kebs[0] if kebs else rebs[0]

    forms: list[Form] = []
    seen_forms: set[tuple[str, str | None]] = set()

    def add_form(text: str, tag: str) -> None:
        key = (text, tag)
        if text and text != headword and key not in seen_forms:
            forms.append(Form(form=text, tag=tag))
            seen_forms.add(key)

    for keb in kebs[1:]:
        add_form(keb, "variant")
    for reb in rebs:
        add_form(reb, "reading")
        add_form(_romanize(reb, transliterator), "romanization")

    priorities = [*_texts(entry_elem, "k_ele/ke_pri"), *_texts(entry_elem, "r_ele/re_pri")]
    frequency = entry_frequency(priorities)

    senses: list[Sense] = []
    relations: list[Relation] = []
    for ordinal, sense_elem in enumerate(entry_elem.findall("sense"), start=1):
        glosses = [g for g in _texts(sense_elem, "gloss") if g]
        if not glosses:
            continue
        definition = "; ".join(_esc(g) for g in glosses)

        pos_pairs = _coded(sense_elem, "pos", entities, stats.unresolved_entities)
        pos_codes = [code for code, _value in pos_pairs]
        misc_pairs = _coded(sense_elem, "misc", entities, stats.unresolved_entities)
        field_pairs = _coded(sense_elem, "field", entities, stats.unresolved_entities)
        dial_pairs = _coded(sense_elem, "dial", entities, stats.unresolved_entities)
        s_inf = _texts(sense_elem, "s_inf")

        senses.append(
            Sense(
                definition=definition,
                pos=_sense_pos(pos_codes, stats.unknown_pos),
                pattern=_sense_pattern(pos_codes),
                label=_sense_label(misc_pairs, field_pairs, dial_pairs, notes=s_inf),
            )
        )

        for xref in _texts(sense_elem, "xref"):
            target = _relation_target(xref)
            if target:
                relations.append(Relation(type="see", target=target, sense_ordinal=ordinal))
        for ant in _texts(sense_elem, "ant"):
            target = _relation_target(ant)
            if target:
                relations.append(Relation(type="antonym", target=target, sense_ordinal=ordinal))

    if not senses:
        stats.skipped_no_senses += 1
        return None

    stats.entries += 1
    return Entry(
        headword=headword,
        lang="ja",
        senses=tuple(senses),
        frequency=frequency,
        forms=tuple(forms),
        relations=tuple(relations),
    )


def iter_entries(dump: Path, entities: dict[str, str]) -> Iterator[Entry]:
    """Canonical entries streamed from the JMdict XML dump, in source order."""
    stats = ConversionStats()
    transliterator = icu.Transliterator.createInstance(_ROMAJI_TRANSFORM)
    opener = gzip.open if dump.suffix == ".gz" else open
    with opener(dump, "rb") as handle:
        context = iter(ET.iterparse(handle, events=("start", "end")))
        _event, root = next(context)  # the "start" event for <JMdict>, the document root
        seen_since_clear = 0
        for event, elem in context:
            if event != "end" or elem.tag != "entry":
                continue
            entry = convert_entry(elem, entities, transliterator, stats)
            elem.clear()
            seen_since_clear += 1
            if seen_since_clear >= _ENTRY_CLEAR_INTERVAL:
                root.clear()
                seen_since_clear = 0
            if entry is not None:
                yield entry
    logger.info(
        "converted %d entries (%d dropped: no usable sense)",
        stats.entries,
        stats.skipped_no_senses,
    )
    if stats.unknown_pos:
        logger.info("parts of speech mapped to 'other': %s", dict(stats.unknown_pos.most_common()))
    if stats.unresolved_entities:
        logger.warning(
            "entity values with no matching declaration: %s",
            dict(stats.unresolved_entities.most_common()),
        )


class JMdictConverter(Converter):
    """The `Converter` contract over the single JMdict dump in one cache directory."""

    source_id: ClassVar[str] = "jmdict"

    def __init__(self, cache_dir: Path) -> None:
        self.cache_dir = cache_dir

    def fetch(self, cache_dir: Path) -> Path:
        return fetch_dump(cache_dir)

    def dictionaries(self) -> list[DictSpec]:
        dump = dump_path(self.cache_dir)
        if not dump.exists():
            return []
        info = read_dump_info(dump)
        return [dictionary_spec(bundle_version(info.created))]

    def iter_records(self, spec: DictSpec) -> Iterator[Entry]:
        if spec.dict_id != DICT_ID:
            raise JMdictError(f"unknown dictionary {spec.dict_id!r}; jmdict only builds {DICT_ID}")
        dump = dump_path(self.cache_dir)
        header = _read_header(dump)
        entities = parse_entities(header)
        return iter_entries(dump, entities)


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.converters.jmdict")
    parser.add_argument("--dict", dest="dict_id", help=f"dictionary to build ({DICT_ID})")
    parser.add_argument(
        "--out", type=Path, default=Path("out"), help="bundles go to OUT/<dict_id>/"
    )
    parser.add_argument("--cache", type=Path, default=Path("sources/jmdict"))
    parser.add_argument("--no-fetch", action="store_true", help="use the cached dump as is")
    parser.add_argument("--fetch-only", action="store_true", help="refresh the dump, build nothing")
    parser.add_argument("--limit", type=int, default=None, help="stop after N entries (testing)")
    parser.add_argument("--build-number", type=int, default=1, help="N in version YYYY.MM.N")
    parser.add_argument("--built-at", default=None, help="fixed build time, YYYY-MM-DDTHH:MM:SSZ")
    parser.add_argument(
        "--no-vacuum",
        action="store_true",
        help="skip the final VACUUM (it needs a temporary copy of the whole bundle)",
    )
    parser.add_argument("--list", action="store_true", help="list the dictionaries and exit")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """CLI: fetch the JMdict dump if needed, convert it, build its bundle."""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    if args.list:
        print(f"{DICT_ID}\t{_NAME}\t{JMDICT_URL}")
        return 0
    if not args.dict_id:
        print("error: --dict is required (see --list)", file=sys.stderr)
        return 1
    if args.dict_id != DICT_ID:
        print(
            f"error: unknown dictionary {args.dict_id!r}; jmdict only builds {DICT_ID}",
            file=sys.stderr,
        )
        return 1

    try:
        dump = dump_path(args.cache)
        if not (args.no_fetch and dump.exists()):
            fetch_dump(args.cache)
        if args.fetch_only:
            return 0
        header = _read_header(dump)
        entities = parse_entities(header)
        created = parse_created_date(header)
        spec = dictionary_spec(bundle_version(created, args.build_number))
        entries: Iterable[Entry] = iter_entries(dump, entities)
        if args.limit is not None:
            entries = islice(entries, args.limit)
        built_at = (
            datetime.strptime(args.built_at, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)
            if args.built_at
            else None
        )
        out_path = build_bundle(
            entries,
            spec,
            args.out / spec.dict_id,
            built_at=built_at,
            extra_meta={
                "source_converter": JMdictConverter.source_id,
                "source_dump_date": created.isoformat(),
            },
            vacuum=not args.no_vacuum,
        )
    except (JMdictError, BuildError, SchemaError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"built {out_path}")
    return 0


__all__ = [
    "DICT_ID",
    "ConversionStats",
    "DumpInfo",
    "JMdictConverter",
    "JMdictError",
    "bundle_version",
    "convert_entry",
    "dictionary_spec",
    "entry_frequency",
    "fetch_dump",
    "iter_entries",
    "map_pos",
    "parse_created_date",
    "parse_entities",
    "read_dump_info",
]

if __name__ == "__main__":
    raise SystemExit(main())
