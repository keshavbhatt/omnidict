"""CC-CEDICT, a community-maintained Chinese-English dictionary, PLAN.md 6.2.

CC-CEDICT publishes one gzipped UTF-8 text file with a `#`-commented header
followed by one line per headword reading:

    Traditional Simplified [pin1 yin1] /gloss 1/gloss 2/.../

Mapping decisions (DOCS/sources.md has the long version):
- the headword is the Simplified form; every line for the same Simplified
  form merges into one entry (lines are not necessarily adjacent: a
  Simplified form can be shared by Traditional forms far apart in the file,
  e.g. `裡`/`里` both simplify to `里`), senses in file order;
- the Traditional form (when different from Simplified) and the pinyin
  (tone-marked, from the numbered syllables) become `forms`; pinyin is
  stored both with its natural syllable spacing and concatenated with no
  spaces, so a query typed either way (`ni hao` or `nihao`) normalizes to a
  stored form (`normalize_headword` only strips diacritics, it does not
  remove spaces, so the two spellings need two forms to both match);
- part of speech is not given by CC-CEDICT and is left `null`;
- a `CL:` gloss (a measure word list) is not its own sense: it becomes the
  `label` of the gloss immediately before it on the same line, the
  simplification CC-CEDICT's own layout suggests (a `CL:` entry always
  follows the sense it classifies, and a line can carry more than one, e.g.
  `棋 棋 [qi2] /chess/.../CL:盤|盘[pan2]/chess piece/CL:個|个[ge4],顆|颗[ke1]/`);
- an embedded cross-reference (`see X[pinyin]`, `variant of X[pinyin]`,
  `also written X|Y[pinyin]`, ...) becomes a `lex:` link on the Chinese
  target text; the bracketed pinyin that follows it is dropped from the
  rendered text (the linked entry shows its own pinyin as a form).

Run: `python -m omnipipe.converters.cedict --dict cedict-zh-en --out out/`
"""

from __future__ import annotations

import argparse
import gzip
import html
import json
import logging
import re
import shutil
import sys
import urllib.error
import urllib.request
from collections.abc import Iterable, Iterator, Mapping
from dataclasses import dataclass, field
from datetime import UTC, date, datetime
from itertools import islice
from pathlib import Path
from typing import ClassVar, TextIO

from omnipipe.build import BuildError, build_bundle
from omnipipe.converters.base import Converter
from omnipipe.schema import DictSpec, Entry, Form, SchemaError, Sense

logger = logging.getLogger(__name__)

CEDICT_URL = "https://www.mdbg.net/chinese/export/cedict/cedict_1_0_ts_utf-8_mdbg.txt.gz"
USER_AGENT = "omnipipe/0.1 (+https://github.com/keshavbhatt/omnidict)"

_DICT_ID = "cedict-zh-en"
_NAME = "Chinese-English (CC-CEDICT)"
_PUBLISHER = "MDBG / CC-CEDICT contributors"
_LICENSE = "CC-BY-SA-4.0"
_LICENSE_URL = "https://creativecommons.org/licenses/by-sa/4.0/"
_ATTRIBUTION = (
    "Data from CC-CEDICT (https://cc-cedict.org/), published by MDBG, "
    "© CC-CEDICT contributors, CC BY-SA 4.0"
)

_LINE_RE = re.compile(r"^(?P<trad>\S+) (?P<simp>\S+) \[(?P<pinyin>[^\]]*)\] /(?P<defs>.*)/\s*$")
_HEADER_DATE_RE = re.compile(r"^#!\s*date=(\S+)\s*$")
_CLASSIFIER_PREFIX = "CL:"
_CLASSIFIER_PIECE_RE = re.compile(r"^(?P<hanzi>[^\[]+)\[(?P<pinyin>[^\]]*)\]$")
# A run of CJK ideographs (optionally split traditional|simplified) directly
# followed by a bracketed pinyin gloss: the shape of every CC-CEDICT
# cross-reference target.
_XREF_RE = re.compile(r"([㐀-鿿豈-﫿]+(?:\|[㐀-鿿豈-﫿]+)?)\[[^\]]*\]")

# Tone-marked vowels for tones 1-4, indexed [tone - 1]; tone 5 (neutral) adds no mark.
_TONE_MARKS: Mapping[str, str] = {
    "a": "āáǎà",
    "e": "ēéěè",
    "i": "īíǐì",
    "o": "ōóǒò",
    "u": "ūúǔù",
    "ü": "ǖǘǚǜ",
}
_VOWELS = frozenset("aeiouü")
_SYLLABLE_RE = re.compile(r"^([A-Za-zÜü]+)([1-5])$")


class CedictError(Exception):
    """Raised when the CC-CEDICT dump cannot be fetched, read, or matched."""


# ---------------------------------------------------------------------------
# Fetch: one dump, no per-language variants.
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class DumpInfo:
    """What was fetched, kept next to the dump as `<file>.json`."""

    url: str
    last_modified: str | None
    fetched_at: str

    def to_json(self) -> dict[str, object]:
        return {"url": self.url, "last_modified": self.last_modified, "fetched_at": self.fetched_at}

    @staticmethod
    def from_json(obj: Mapping[str, object]) -> DumpInfo:
        last_modified = obj.get("last_modified")
        return DumpInfo(
            url=str(obj["url"]),
            last_modified=str(last_modified) if last_modified is not None else None,
            fetched_at=str(obj["fetched_at"]),
        )


def dump_path(cache_dir: Path) -> Path:
    return cache_dir / "cedict_1_0_ts_utf-8_mdbg.txt.gz"


def read_dump_info(dump: Path) -> DumpInfo:
    info_path = dump.with_name(dump.name + ".json")
    if not info_path.exists():
        raise CedictError(f"{info_path} is missing: fetch the dump first")
    obj = json.loads(info_path.read_text(encoding="utf-8"))
    if not isinstance(obj, Mapping):
        raise CedictError(f"{info_path} is not a JSON object")
    return DumpInfo.from_json(obj)


def fetch_dump(cache_dir: Path, *, timeout: float = 120.0) -> Path:
    """Download the CC-CEDICT dump into `cache_dir` unless the copy there is current.

    Sends If-Modified-Since when a previous copy exists, so a refresh that
    finds nothing new costs one request (mirrors `converters/kaikki.py`).
    """
    cache_dir.mkdir(parents=True, exist_ok=True)
    target = dump_path(cache_dir)
    info_path = target.with_name(target.name + ".json")
    headers = {"User-Agent": USER_AGENT}
    previous = read_dump_info(target) if target.exists() and info_path.exists() else None
    if previous is not None and previous.last_modified:
        headers["If-Modified-Since"] = previous.last_modified

    partial = target.with_name(target.name + ".part")
    request = urllib.request.Request(CEDICT_URL, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            last_modified = response.headers.get("Last-Modified")
            with partial.open("wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)
    except urllib.error.HTTPError as exc:
        if exc.code == 304 and previous is not None:
            logger.info("%s is current (%s)", target, previous.last_modified)
            return target
        raise CedictError(f"cannot fetch {CEDICT_URL}: HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise CedictError(f"cannot fetch {CEDICT_URL}: {exc.reason}") from exc

    partial.replace(target)
    fetched_at = datetime.now(UTC).replace(microsecond=0)
    info = DumpInfo(CEDICT_URL, last_modified, fetched_at.strftime("%Y-%m-%dT%H:%M:%SZ"))
    info_path.write_text(json.dumps(info.to_json(), indent=2) + "\n", encoding="utf-8")
    logger.info("fetched %s (%s)", target, last_modified)
    return target


def _open_text(dump: Path) -> TextIO:
    if dump.suffix == ".gz":
        return gzip.open(dump, "rt", encoding="utf-8")
    return dump.open(encoding="utf-8")


def read_header_date(dump: Path) -> date:
    """The `#! date=...` header line as a date: CC-CEDICT's own version stamp."""
    with _open_text(dump) as handle:
        for line in handle:
            stripped = line.strip()
            if not stripped.startswith("#"):
                break
            match = _HEADER_DATE_RE.match(stripped)
            if match:
                return (
                    datetime.strptime(match.group(1), "%Y-%m-%dT%H:%M:%SZ")
                    .replace(tzinfo=UTC)
                    .date()
                )
    raise CedictError(f"{dump}: no '#! date=' header line found")


def bundle_version(header_date: date, build: int = 1) -> str:
    """`YYYY.MM.N`: the month of CC-CEDICT's own header date and a rebuild counter."""
    return f"{header_date.year}.{header_date.month:02d}.{build}"


def read_lines(dump: Path) -> Iterator[str]:
    """Yield the dump's lines, decompressed if needed."""
    with _open_text(dump) as handle:
        yield from handle


def spec_for(version: str) -> DictSpec:
    return DictSpec(
        dict_id=_DICT_ID,
        name=_NAME,
        source_lang="zh",
        target_lang="en",
        version=version,
        publisher=_PUBLISHER,
        license=_LICENSE,
        license_url=_LICENSE_URL,
        attribution=_ATTRIBUTION,
        kind="bilingual",
    )


# ---------------------------------------------------------------------------
# Pinyin: numbered tone digits (ni3 hao3) -> tone marks (nǐ hǎo).
# ---------------------------------------------------------------------------


def _apply_tone(vowel: str, tone: int) -> str:
    if tone == 5:
        return vowel
    marked = _TONE_MARKS[vowel.lower()][tone - 1]
    return marked.upper() if vowel.isupper() else marked


def _tone_mark_index(base: str) -> int | None:
    """Which letter of `base` gets the tone mark (the standard pinyin rule)."""
    lower = base.lower()
    if "a" in lower:
        return lower.index("a")
    if "e" in lower:
        return lower.index("e")
    if "ou" in lower:
        return lower.index("o")
    positions = [i for i, ch in enumerate(lower) if ch in _VOWELS]
    return positions[-1] if positions else None


def convert_syllable(syllable: str) -> str:
    """One numbered pinyin syllable (`ni3`, `lu:4`, `Zhong1`) to tone marks."""
    text = syllable.replace("u:", "ü").replace("U:", "Ü")
    text = text.replace("v", "ü").replace("V", "Ü")
    match = _SYLLABLE_RE.match(text)
    if match is None:
        return text
    base, tone_str = match.group(1), match.group(2)
    index = _tone_mark_index(base)
    if index is None:
        return base
    chars = list(base)
    chars[index] = _apply_tone(chars[index], int(tone_str))
    return "".join(chars)


def convert_pinyin(numbered: str) -> str:
    """A whole numbered-pinyin field, syllable by syllable, spaces kept as given."""
    return " ".join(convert_syllable(token) for token in numbered.split(" ") if token)


def pinyin_forms(numbered: str) -> tuple[str, str]:
    """(spaced, concatenated) tone-marked pinyin for `numbered`.

    Both are kept as `forms`: normalization strips diacritics but not
    spaces, so "ni hao" only matches a spaced form and "nihao" only a
    concatenated one.
    """
    syllables = [convert_syllable(token) for token in numbered.split(" ") if token]
    return " ".join(syllables), "".join(syllables)


# ---------------------------------------------------------------------------
# Markup: cross-reference glosses become `lex:` links.
# ---------------------------------------------------------------------------


def _esc(text: str) -> str:
    return html.escape(text, quote=False)


def gloss_html(text: str) -> str:
    """`text` with embedded `TARGET[pinyin]` cross-references as `lex:` links.

    The bracketed pinyin is dropped from the rendered text; the linked entry
    carries its own pinyin as a form.
    """
    parts: list[str] = []
    position = 0
    for match in _XREF_RE.finditer(text):
        target = match.group(1)
        simplified = target.split("|")[-1]
        if not simplified:
            continue
        parts.append(_esc(text[position : match.start()]))
        href = html.escape("lex:" + simplified, quote=True)
        parts.append(f'<a href="{href}">{_esc(target)}</a>')
        position = match.end()
    parts.append(_esc(text[position:]))
    return "".join(parts)


def _format_classifier_piece(piece: str) -> str:
    piece = piece.strip()
    match = _CLASSIFIER_PIECE_RE.match(piece)
    if match is None:
        return piece
    hanzi = match.group("hanzi").replace("|", "/")
    spaced, _ = pinyin_forms(match.group("pinyin"))
    return f"{hanzi} ({spaced})" if spaced else hanzi


# ---------------------------------------------------------------------------
# Line parsing and record conversion.
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class ParsedLine:
    traditional: str
    simplified: str
    pinyin: str
    defs: tuple[str, ...]


def parse_line(line: str) -> ParsedLine | None:
    """One CC-CEDICT data line, or `None` if it does not match the format."""
    match = _LINE_RE.match(line)
    if match is None:
        return None
    defs = tuple(d for d in match.group("defs").split("/") if d)
    if not defs:
        return None
    return ParsedLine(
        traditional=match.group("trad"),
        simplified=match.group("simp"),
        pinyin=match.group("pinyin"),
        defs=defs,
    )


@dataclass(slots=True)
class ConversionStats:
    lines: int = 0
    entries: int = 0
    malformed: int = 0
    empty: int = 0


@dataclass(slots=True)
class _Accumulator:
    """The entry being assembled from every line seen for one Simplified form."""

    simplified: str
    senses: list[Sense] = field(default_factory=list)
    forms: list[Form] = field(default_factory=list)
    _seen_forms: set[tuple[str, str | None]] = field(default_factory=set)

    def add_form(self, text: str, tag: str) -> None:
        key = (text, tag)
        if text and text != self.simplified and key not in self._seen_forms:
            self._seen_forms.add(key)
            self.forms.append(Form(form=text, tag=tag))

    def add_line(self, parsed: ParsedLine) -> None:
        # A `CL:` gloss attaches to the definition immediately before it on
        # this line (DOCS/sources.md has why); collect per-def classifier
        # pieces first, then build senses once the whole line is seen.
        pending: list[tuple[str, list[str]]] = []
        for raw_def in parsed.defs:
            if raw_def.startswith(_CLASSIFIER_PREFIX):
                raw_pieces = raw_def[len(_CLASSIFIER_PREFIX) :].split(",")
                pieces = [_format_classifier_piece(p) for p in raw_pieces]
                if pending:
                    pending[-1][1].extend(pieces)
                continue
            pending.append((raw_def, []))

        for raw_def, classifier_pieces in pending:
            definition = gloss_html(raw_def.strip())
            if not definition:
                continue
            label = "CL: " + "; ".join(classifier_pieces) if classifier_pieces else None
            self.senses.append(Sense(definition=definition, label=label))

        if parsed.traditional != parsed.simplified:
            self.add_form(parsed.traditional, "traditional")
        spaced, concatenated = pinyin_forms(parsed.pinyin)
        self.add_form(spaced, "pinyin")
        if concatenated != spaced:
            self.add_form(concatenated, "pinyin")

    def entry(self) -> Entry | None:
        if not self.senses:
            return None
        return Entry(
            headword=self.simplified,
            lang="zh",
            senses=tuple(self.senses),
            forms=tuple(self.forms),
        )


def convert_lines(lines: Iterable[str], stats: ConversionStats | None = None) -> Iterator[Entry]:
    """Canonical entries from CC-CEDICT data lines, grouped by Simplified form.

    Lines for the same Simplified form are not necessarily adjacent (a
    Traditional variant can sort far away), so every line is folded into a
    dictionary keyed by Simplified form rather than merged consecutively;
    entries are yielded in order of each Simplified form's first line.
    """
    stats = stats if stats is not None else ConversionStats()
    accumulators: dict[str, _Accumulator] = {}
    for raw_line in lines:
        line = raw_line.rstrip("\n")
        if not line.strip() or line.startswith("#"):
            continue
        stats.lines += 1
        parsed = parse_line(line)
        if parsed is None:
            stats.malformed += 1
            logger.warning("skipping malformed line: %r", line)
            continue
        accumulator = accumulators.setdefault(
            parsed.simplified, _Accumulator(simplified=parsed.simplified)
        )
        before = len(accumulator.senses)
        accumulator.add_line(parsed)
        if len(accumulator.senses) == before:
            stats.empty += 1

    for accumulator in accumulators.values():
        entry = accumulator.entry()
        if entry is not None:
            stats.entries += 1
            yield entry


class CedictConverter(Converter):
    """The `Converter` contract over one cached CC-CEDICT dump."""

    source_id: ClassVar[str] = "cedict"

    def __init__(self, cache_dir: Path) -> None:
        self.cache_dir = cache_dir
        self.stats = ConversionStats()

    def fetch(self, cache_dir: Path) -> Path:
        return fetch_dump(cache_dir)

    def dictionaries(self) -> list[DictSpec]:
        dump = dump_path(self.cache_dir)
        if not dump.exists():
            return []
        return [spec_for(bundle_version(read_header_date(dump)))]

    def iter_records(self, spec: DictSpec) -> Iterator[Entry]:
        if spec.dict_id != _DICT_ID:
            raise CedictError(
                f"unknown dictionary {spec.dict_id!r}; cedict only builds {_DICT_ID!r}"
            )
        dump = dump_path(self.cache_dir)
        return convert_lines(read_lines(dump), stats=self.stats)


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.converters.cedict")
    parser.add_argument("--dict", dest="dict_id", help=f"dictionary to build ({_DICT_ID})")
    parser.add_argument(
        "--out", type=Path, default=Path("out"), help="bundles go to OUT/<dict_id>/"
    )
    parser.add_argument("--cache", type=Path, default=Path("sources/cedict"))
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
    """CLI: fetch the CC-CEDICT dump if needed, convert it, build its bundle."""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    if args.list:
        print(f"{_DICT_ID}\t{_NAME}\t{CEDICT_URL}")
        return 0
    if not args.dict_id:
        print("error: --dict is required (see --list)", file=sys.stderr)
        return 1
    if args.dict_id != _DICT_ID:
        print(
            f"error: unknown dictionary {args.dict_id!r}; cedict only builds {_DICT_ID!r}",
            file=sys.stderr,
        )
        return 1

    try:
        dump = dump_path(args.cache)
        if not (args.no_fetch and dump.exists()):
            fetch_dump(args.cache)
        if args.fetch_only:
            return 0
        header_date = read_header_date(dump)
        spec = spec_for(bundle_version(header_date, args.build_number))
        converter = CedictConverter(args.cache)
        entries: Iterable[Entry] = converter.iter_records(spec)
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
                "source_converter": CedictConverter.source_id,
                "source_dump_date": header_date.isoformat(),
            },
            vacuum=not args.no_vacuum,
        )
    except (CedictError, BuildError, SchemaError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    stats = converter.stats
    print(
        f"built {out_path}: {stats.entries} entries from {stats.lines} lines "
        f"({stats.malformed} malformed, {stats.empty} without a usable definition, skipped)"
    )
    return 0


__all__ = [
    "CEDICT_URL",
    "CedictConverter",
    "CedictError",
    "ConversionStats",
    "DumpInfo",
    "ParsedLine",
    "bundle_version",
    "convert_lines",
    "convert_pinyin",
    "convert_syllable",
    "fetch_dump",
    "gloss_html",
    "parse_line",
    "pinyin_forms",
    "read_dump_info",
    "read_header_date",
    "read_lines",
    "spec_for",
]

if __name__ == "__main__":
    raise SystemExit(main())
