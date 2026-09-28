"""Open English WordNet, WN-LMF XML (PLAN.md 6.2).

The Open English WordNet (https://github.com/globalwordnet/english-wordnet) is the
maintained successor of the Princeton WordNet, released under CC BY 4.0 (confirmed in
the repository's `LICENSE.md` and `README.md`; attribution is required to both
Princeton WordNet and the Open English Wordnet team). One release asset, the
WN-LMF XML dump (`english-wordnet-<edition>.xml.gz`), is fetched and stream-parsed
with `xml.etree.ElementTree.iterparse` (DOCS/sources.md).

Mapping decisions (DOCS/sources.md):
- one canonical entry per written form (`Lemma/@writtenForm`): the lexical entries
  that share a written form across parts of speech merge into one entry, senses
  grouped by part of speech in the order noun, verb, adj (`a`/`s` together), adv;
- each `Sense` becomes one sense, its definition is the sense's `Synset/Definition`
  (joined if a synset carries more than one), its examples are the synset's
  `Example` elements (at most 3);
- a sense's synonyms are its synset's other members (at most 10), as `synonym`
  relations; `SenseRelation antonym` becomes an `antonym` relation; a synset's
  `hypernym` relations become `see` relations (at most 5) pointing at the first
  member of the target synset, resolved once every synset has been read;
- `Form` elements are the lexical entry's irregular inflections, tag `inflection`;
- `Pronunciation` elements (IPA, `@variety`) are kept where the file has them;
  `GB` is recorded as region `UK` to match the other converters, every other
  variety code (`US`, `CA`, `AU`, `IE`, `NZ`, `SG`, `ZA`) is kept as is.

The WN-LMF file lists every `LexicalEntry` before any `Synset` (verified against
the 2025 edition), so one streaming pass is enough: lexical entries are read into
memory first, synsets are read and resolved against them as they arrive, and
`hypernym` targets (which may point forwards in the synset section) are resolved
once the whole file has been read.

Run: `python -m omnipipe.converters.oewn --out out/`
"""

from __future__ import annotations

import argparse
import gzip
import html
import json
import logging
import shutil
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from collections.abc import Iterator, Sequence
from dataclasses import dataclass, field
from datetime import UTC, date, datetime
from email.utils import parsedate_to_datetime
from itertools import islice
from pathlib import Path
from typing import ClassVar

from omnipipe.build import BuildError, build_bundle
from omnipipe.converters.base import Converter
from omnipipe.schema import DictSpec, Entry, Example, Form, Pronunciation, Relation, Sense

logger = logging.getLogger(__name__)

OEWN_RELEASE = "2025-edition"
OEWN_XML_URL = (
    "https://github.com/globalwordnet/english-wordnet/releases/"
    "download/2025-edition/english-wordnet-2025.xml.gz"
)
USER_AGENT = "omnipipe/0.1 (+https://github.com/keshavbhatt/omnidict)"

DICT_ID = "oewn-en"
_NAME = "English WordNet (Open English WordNet)"
_PUBLISHER = "Open English WordNet contributors"
_LICENSE = "CC-BY-4.0"
_LICENSE_URL = "https://creativecommons.org/licenses/by/4.0/"
_ATTRIBUTION = (
    "Open English WordNet, (c) 2019-present The Open English WordNet team, derived from "
    "the Princeton WordNet, CC BY 4.0"
)

# WN-LMF part-of-speech codes to the schema's POS set (PLAN.md 4.4).
_POS_MAP: dict[str, str] = {"n": "noun", "v": "verb", "a": "adj", "s": "adj", "r": "adv"}
# Merge order for one written form's lexical entries: noun, verb, adj (a and s
# together), adv.
_POS_ORDER: dict[str, int] = {"n": 0, "v": 1, "a": 2, "s": 2, "r": 3}
_REGION_MAP: dict[str, str] = {"GB": "UK"}

_MAX_EXAMPLES_PER_SENSE = 3
_MAX_SYNONYMS_PER_SENSE = 10
_MAX_ANTONYMS_PER_SENSE = 10
_MAX_SEE_PER_SENSE = 5


class OewnError(Exception):
    """Raised when the dump cannot be fetched or does not parse as WN-LMF."""


@dataclass(frozen=True, slots=True)
class DumpInfo:
    """What was fetched, kept next to the dump as `<file>.json`."""

    url: str
    last_modified: str | None
    dump_date: date
    fetched_at: str

    def to_json(self) -> dict[str, object]:
        return {
            "url": self.url,
            "last_modified": self.last_modified,
            "dump_date": self.dump_date.isoformat(),
            "fetched_at": self.fetched_at,
        }

    @staticmethod
    def from_json(obj: dict[str, object]) -> DumpInfo:
        last_modified = obj.get("last_modified")
        return DumpInfo(
            url=str(obj["url"]),
            last_modified=str(last_modified) if last_modified is not None else None,
            dump_date=date.fromisoformat(str(obj["dump_date"])),
            fetched_at=str(obj["fetched_at"]),
        )


def dump_path(cache_dir: Path) -> Path:
    return cache_dir / "english-wordnet.xml.gz"


def read_dump_info(dump: Path) -> DumpInfo:
    info_path = dump.with_name(dump.name + ".json")
    if not info_path.exists():
        raise OewnError(f"{info_path} is missing: fetch the dump first")
    obj = json.loads(info_path.read_text(encoding="utf-8"))
    if not isinstance(obj, dict):
        raise OewnError(f"{info_path} is not a JSON object")
    return DumpInfo.from_json(obj)


def fetch_dump(cache_dir: Path, *, timeout: float = 600.0) -> Path:
    """Download the WN-LMF dump into `cache_dir` unless the copy there is current.

    Sends If-Modified-Since when a previous copy exists, mirroring the Kaikki
    converter's fetch (DOCS/sources.md).
    """
    cache_dir.mkdir(parents=True, exist_ok=True)
    target = dump_path(cache_dir)
    info_path = target.with_name(target.name + ".json")
    headers = {"User-Agent": USER_AGENT}
    previous = read_dump_info(target) if target.exists() and info_path.exists() else None
    if previous is not None and previous.last_modified:
        headers["If-Modified-Since"] = previous.last_modified

    partial = target.with_name(target.name + ".part")
    request = urllib.request.Request(OEWN_XML_URL, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            last_modified = response.headers.get("Last-Modified")
            with partial.open("wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)
    except urllib.error.HTTPError as exc:
        if exc.code == 304 and previous is not None:
            logger.info("%s is current (%s)", target, previous.last_modified)
            return target
        raise OewnError(f"cannot fetch {OEWN_XML_URL}: HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise OewnError(f"cannot fetch {OEWN_XML_URL}: {exc.reason}") from exc

    partial.replace(target)
    fetched_at = datetime.now(UTC).replace(microsecond=0)
    dump_date = (
        parsedate_to_datetime(last_modified).astimezone(UTC).date()
        if last_modified
        else fetched_at.date()
    )
    info = DumpInfo(
        OEWN_XML_URL, last_modified, dump_date, fetched_at.strftime("%Y-%m-%dT%H:%M:%SZ")
    )
    info_path.write_text(json.dumps(info.to_json(), indent=2) + "\n", encoding="utf-8")
    logger.info("fetched %s (%s)", target, last_modified)
    return target


def bundle_version(dump_date: date, build: int = 1) -> str:
    """`YYYY.MM.N`: the month the dump was fetched and a rebuild counter, matching
    the other converters' version scheme (DOCS/sources.md)."""
    return f"{dump_date.year}.{dump_date.month:02d}.{build}"


def spec_for(version: str) -> DictSpec:
    return DictSpec(
        dict_id=DICT_ID,
        name=_NAME,
        source_lang="en",
        target_lang="en",
        version=version,
        publisher=_PUBLISHER,
        license=_LICENSE,
        license_url=_LICENSE_URL,
        attribution=_ATTRIBUTION,
        kind="monolingual",
        source_lang_name="English",
        target_lang_name="English",
        source_url=OEWN_XML_URL,
    )


def _esc(text: str) -> str:
    return html.escape(text, quote=False)


# ---------------------------------------------------------------------------
# WN-LMF parsing
# ---------------------------------------------------------------------------


@dataclass(slots=True)
class _LexSense:
    sense_id: str
    synset_id: str
    antonym_targets: list[str] = field(default_factory=list)


@dataclass(slots=True)
class _LexEntry:
    entry_id: str
    written_form: str
    pos: str
    pronunciations: list[tuple[str, str | None]] = field(default_factory=list)
    forms: list[str] = field(default_factory=list)
    senses: list[_LexSense] = field(default_factory=list)


@dataclass(slots=True)
class _Synset:
    pos: str
    definition: str
    examples: list[str] = field(default_factory=list)
    member_entry_ids: list[str] = field(default_factory=list)
    hypernym_targets: list[str] = field(default_factory=list)


def _local_tag(elem: ET.Element) -> str:
    tag = elem.tag
    return tag.rsplit("}", 1)[-1] if "}" in tag else tag


def _parse_lexical_entry(elem: ET.Element) -> _LexEntry | None:
    entry_id = elem.get("id")
    lemma = elem.find("Lemma")
    if entry_id is None or lemma is None:
        return None
    written_form = lemma.get("writtenForm")
    pos = lemma.get("partOfSpeech")
    if not written_form or pos not in _POS_MAP:
        return None

    pronunciations: list[tuple[str, str | None]] = []
    for pron in lemma.findall("Pronunciation"):
        ipa = (pron.text or "").strip()
        if not ipa:
            continue
        variety = pron.get("variety")
        region = _REGION_MAP.get(variety, variety) if variety else None
        if (ipa, region) not in pronunciations:
            pronunciations.append((ipa, region))

    forms: list[str] = []
    for form_elem in elem.findall("Form"):
        text = (form_elem.get("writtenForm") or "").strip()
        if text and text != written_form and text not in forms:
            forms.append(text)

    senses: list[_LexSense] = []
    for sense_elem in elem.findall("Sense"):
        sense_id = sense_elem.get("id")
        synset_id = sense_elem.get("synset")
        if not sense_id or not synset_id:
            continue
        antonyms = [
            rel.get("target", "")
            for rel in sense_elem.findall("SenseRelation")
            if rel.get("relType") == "antonym" and rel.get("target")
        ]
        senses.append(_LexSense(sense_id=sense_id, synset_id=synset_id, antonym_targets=antonyms))

    return _LexEntry(
        entry_id=entry_id,
        written_form=written_form,
        pos=pos,
        pronunciations=pronunciations,
        forms=forms,
        senses=senses,
    )


def _parse_synset(elem: ET.Element) -> tuple[str, _Synset] | None:
    synset_id = elem.get("id")
    pos = elem.get("partOfSpeech")
    if not synset_id or pos not in _POS_MAP:
        return None
    definitions = [
        (d.text or "").strip() for d in elem.findall("Definition") if (d.text or "").strip()
    ]
    if not definitions:
        return None
    examples = [(e.text or "").strip() for e in elem.findall("Example") if (e.text or "").strip()]
    members_attr = elem.get("members", "")
    member_entry_ids = members_attr.split() if members_attr else []
    hypernyms = [
        rel.get("target", "")
        for rel in elem.findall("SynsetRelation")
        if rel.get("relType") == "hypernym" and rel.get("target")
    ]
    return synset_id, _Synset(
        pos=pos,
        definition="; ".join(definitions),
        examples=examples,
        member_entry_ids=member_entry_ids,
        hypernym_targets=hypernyms,
    )


@dataclass(slots=True)
class ParsedDump:
    entries: dict[str, _LexEntry]
    sense_to_entry: dict[str, str]
    synsets: dict[str, _Synset]


def parse_dump(dump: Path) -> ParsedDump:
    """Stream `dump` once, collecting every lexical entry and synset.

    `LexicalEntry` elements precede every `Synset` in the file, so this single
    pass is enough; a `Synset`'s `hypernym` targets may point forward to a
    synset read later and are resolved afterwards by the caller.
    """
    entries: dict[str, _LexEntry] = {}
    sense_to_entry: dict[str, str] = {}
    synsets: dict[str, _Synset] = {}

    opener = gzip.open if dump.suffix == ".gz" else open
    with opener(dump, "rb") as handle:
        for _event, elem in ET.iterparse(handle, events=("end",)):
            tag = _local_tag(elem)
            if tag == "LexicalEntry":
                entry = _parse_lexical_entry(elem)
                if entry is not None:
                    entries[entry.entry_id] = entry
                    for sense in entry.senses:
                        sense_to_entry[sense.sense_id] = entry.entry_id
                elem.clear()
            elif tag == "Synset":
                parsed = _parse_synset(elem)
                if parsed is not None:
                    synsets[parsed[0]] = parsed[1]
                elem.clear()
    return ParsedDump(entries=entries, sense_to_entry=sense_to_entry, synsets=synsets)


def _synset_headword(parsed: ParsedDump, synset_id: str) -> str | None:
    synset = parsed.synsets.get(synset_id)
    if synset is None:
        return None
    for entry_id in synset.member_entry_ids:
        entry = parsed.entries.get(entry_id)
        if entry is not None:
            return entry.written_form
    return None


def _synonym_words(parsed: ParsedDump, synset_id: str, headword: str) -> list[str]:
    synset = parsed.synsets.get(synset_id)
    if synset is None:
        return []
    words: list[str] = []
    for entry_id in synset.member_entry_ids:
        entry = parsed.entries.get(entry_id)
        if entry is not None and entry.written_form != headword and entry.written_form not in words:
            words.append(entry.written_form)
    return words


def convert_dump(parsed: ParsedDump) -> Iterator[Entry]:
    """Canonical entries built from a parsed WN-LMF dump, one per written form."""
    by_headword: dict[str, list[_LexEntry]] = {}
    for entry in parsed.entries.values():
        by_headword.setdefault(entry.written_form, []).append(entry)

    for headword, unsorted_entries in by_headword.items():
        lex_entries = sorted(unsorted_entries, key=lambda e: _POS_ORDER[e.pos])
        senses: list[Sense] = []
        pronunciations: list[Pronunciation] = []
        forms: list[Form] = []
        relations: list[Relation] = []
        seen_pron: set[tuple[str, str | None]] = set()
        seen_forms: set[str] = set()

        for lex in lex_entries:
            mapped_pos = _POS_MAP[lex.pos]
            for ipa, region in lex.pronunciations:
                if (ipa, region) not in seen_pron:
                    seen_pron.add((ipa, region))
                    pronunciations.append(Pronunciation(ipa=ipa, region=region))
            for form_text in lex.forms:
                if form_text not in seen_forms:
                    seen_forms.add(form_text)
                    forms.append(Form(form=form_text, tag="inflection"))

            for lsense in lex.senses:
                synset = parsed.synsets.get(lsense.synset_id)
                if synset is None:
                    continue
                examples = tuple(
                    Example(text=_esc(text)) for text in synset.examples[:_MAX_EXAMPLES_PER_SENSE]
                )
                senses.append(
                    Sense(
                        definition=_esc(synset.definition),
                        pos=mapped_pos,
                        examples=examples,
                    )
                )
                ordinal = len(senses)

                for word in _synonym_words(parsed, lsense.synset_id, headword)[
                    :_MAX_SYNONYMS_PER_SENSE
                ]:
                    relations.append(Relation(type="synonym", target=word, sense_ordinal=ordinal))

                antonym_words: list[str] = []
                for target_sense_id in lsense.antonym_targets:
                    target_entry_id = parsed.sense_to_entry.get(target_sense_id)
                    if target_entry_id is None:
                        continue
                    target_entry = parsed.entries.get(target_entry_id)
                    if (
                        target_entry is not None
                        and target_entry.written_form != headword
                        and target_entry.written_form not in antonym_words
                    ):
                        antonym_words.append(target_entry.written_form)
                for word in antonym_words[:_MAX_ANTONYMS_PER_SENSE]:
                    relations.append(Relation(type="antonym", target=word, sense_ordinal=ordinal))

                see_words: list[str] = []
                for target_synset_id in synset.hypernym_targets:
                    hypernym_word = _synset_headword(parsed, target_synset_id)
                    if (
                        hypernym_word
                        and hypernym_word != headword
                        and hypernym_word not in see_words
                    ):
                        see_words.append(hypernym_word)
                for word in see_words[:_MAX_SEE_PER_SENSE]:
                    relations.append(Relation(type="see", target=word, sense_ordinal=ordinal))

        if not senses:
            continue
        yield Entry(
            headword=headword,
            lang="en",
            senses=tuple(senses),
            pronunciations=tuple(pronunciations),
            forms=tuple(forms),
            relations=tuple(relations),
        )


class OewnConverter(Converter):
    """The `Converter` contract over one cached WN-LMF dump."""

    source_id: ClassVar[str] = "oewn"

    def __init__(self, cache_dir: Path) -> None:
        self.cache_dir = cache_dir

    def fetch(self, cache_dir: Path) -> Path:
        return fetch_dump(cache_dir)

    def dictionaries(self) -> list[DictSpec]:
        dump = dump_path(self.cache_dir)
        if not dump.exists():
            return []
        return [spec_for(bundle_version(read_dump_info(dump).dump_date))]

    def iter_records(self, spec: DictSpec) -> Iterator[Entry]:
        if spec.dict_id != DICT_ID:
            raise OewnError(f"unknown dictionary {spec.dict_id!r}; oewn can only build {DICT_ID}")
        dump = dump_path(self.cache_dir)
        parsed = parse_dump(dump)
        return convert_dump(parsed)


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.converters.oewn")
    parser.add_argument("--dict", dest="dict_id", default=DICT_ID, help="dictionary to build")
    parser.add_argument(
        "--out", type=Path, default=Path("out"), help="bundles go to OUT/<dict_id>/"
    )
    parser.add_argument("--cache", type=Path, default=Path("sources/oewn"))
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
    """CLI: fetch the WN-LMF dump if needed, convert it, build the bundle."""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    if args.list:
        print(f"{DICT_ID}\t{_NAME}\t{OEWN_XML_URL}")
        return 0
    if args.dict_id != DICT_ID:
        print(
            f"error: unknown dictionary {args.dict_id!r}; oewn can only build {DICT_ID}",
            file=sys.stderr,
        )
        return 1

    try:
        dump = dump_path(args.cache)
        if not (args.no_fetch and dump.exists()):
            fetch_dump(args.cache)
        if args.fetch_only:
            return 0
        info = read_dump_info(dump)
        spec = spec_for(bundle_version(info.dump_date, args.build_number))
        parsed = parse_dump(dump)
        entries: Sequence[Entry] | Iterator[Entry] = convert_dump(parsed)
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
                "source_converter": OewnConverter.source_id,
                "source_dump_date": info.dump_date.isoformat(),
            },
            vacuum=not args.no_vacuum,
        )
    except (OewnError, BuildError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"built {out_path} from {len(parsed.entries)} lexical entries")
    return 0


__all__ = [
    "DICT_ID",
    "OEWN_XML_URL",
    "DumpInfo",
    "OewnConverter",
    "OewnError",
    "ParsedDump",
    "bundle_version",
    "convert_dump",
    "fetch_dump",
    "parse_dump",
    "spec_for",
]

if __name__ == "__main__":
    raise SystemExit(main())
