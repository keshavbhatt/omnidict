"""Wiktionary via kaikki.org (wiktextract JSONL), PLAN.md 6.2.

Kaikki publishes one gzipped JSONL file per language of the English
Wiktionary: every line is one (word, part of speech, etymology) record with
English glosses. For a language other than English that is a bilingual
`<lang> -> en` dictionary; for English itself it is a monolingual one.

Mapping decisions (DOCS/sources.md):
- consecutive records for the same word merge into one entry, senses in
  source order, so a headword appears once in a result list;
- records whose every sense is an inflection ("form-of") are skipped: the
  lemma lists the same inflections as `forms`, which is how a lookup of an
  inflected word finds its lemma;
- grammar tags (gender, transitivity, ...) become the sense `pattern`, usage
  tags (formal, slang, ...) its `label`;
- wiki links inside a gloss become `lex:` links;
- romanizations are kept as forms, so Latin-script input finds the entry.

Run: `python -m omnipipe.converters.kaikki --dict wikt-hi-en --out out/`
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
import urllib.parse
import urllib.request
import zlib
from collections import Counter
from collections.abc import Iterable, Iterator, Mapping, Sequence
from dataclasses import dataclass, field
from datetime import UTC, date, datetime
from email.utils import parsedate_to_datetime
from itertools import islice
from pathlib import Path
from typing import ClassVar

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

KAIKKI_BASE_URL = "https://kaikki.org/dictionary"
USER_AGENT = "omnipipe/0.1 (+https://github.com/keshavbhatt/omnidict)"

_PUBLISHER = "Wiktionary contributors"
_LICENSE = "CC-BY-SA-4.0"
_LICENSE_URL = "https://creativecommons.org/licenses/by-sa/4.0/"
_ATTRIBUTION = (
    "Data from Wiktionary via kaikki.org (wiktextract), © Wiktionary contributors, CC BY-SA 4.0"
)

_POS_MAP: Mapping[str, str] = {
    "noun": "noun",
    "name": "noun",
    "verb": "verb",
    "adj": "adj",
    "adv": "adv",
    "pron": "pron",
    "prep": "prep",
    "postp": "prep",
    "circumpos": "prep",
    "conj": "conj",
    "intj": "interj",
    "det": "det",
    "article": "det",
    "num": "num",
    "particle": "particle",
    "prefix": "prefix",
    "suffix": "suffix",
    "phrase": "phrase",
    "prep_phrase": "phrase",
    "proverb": "phrase",
    "abbrev": "abbr",
}
# Records of these kinds describe a spelling, not a word.
_SKIP_POS = frozenset({"romanization"})

# Sense tags shown as the grammar pattern, in this order.
_GRAMMAR_TAGS: Sequence[str] = (
    "masculine",
    "feminine",
    "neuter",
    "countable",
    "uncountable",
    "transitive",
    "intransitive",
    "ambitransitive",
    "reflexive",
    "pronominal",
    "impersonal",
    "auxiliary",
    "copulative",
    "indeclinable",
    "plural-only",
    "singular-only",
    "not-comparable",
)
# Sense tags shown as the usage label.
_USAGE_TAGS = frozenset(
    {
        "formal",
        "informal",
        "colloquial",
        "slang",
        "vulgar",
        "derogatory",
        "offensive",
        "humorous",
        "euphemistic",
        "rare",
        "archaic",
        "obsolete",
        "dated",
        "historical",
        "poetic",
        "literary",
        "figuratively",
        "idiomatic",
        "dialectal",
        "regional",
        "childish",
        "honorific",
        "intimate",
        "familiar",
        "nonstandard",
        "proscribed",
        "uncommon",
    }
)
_REGION_TAGS: Mapping[str, str] = {
    "US": "US",
    "General-American": "US",
    "UK": "UK",
    "British": "UK",
    "Received-Pronunciation": "UK",
    "Australia": "AU",
    "Canada": "CA",
    "Ireland": "IE",
    "New-Zealand": "NZ",
    "India": "IN",
    "Spain": "ES",
    "Castilian": "ES",
    "Latin-America": "Latin America",
    "Mexico": "MX",
    "Argentina": "AR",
}
# Form tags that mark inflection-table bookkeeping rather than a word form.
_NOISE_FORM_TAGS = frozenset({"table-tags", "inflection-template", "class"})

_MAX_EXAMPLES_PER_SENSE = 3
_MAX_EXAMPLE_CHARS = 300
_MAX_PRONUNCIATIONS = 4
_MAX_FORMS = 150
_MAX_NYMS = 20
_MAX_DERIVED = 30
_MAX_RELATED = 20

_WORD_CHAR_RE = re.compile(r"\w")
_NON_WORD_RE = re.compile(r"\W")

type Json = Mapping[str, object]


class KaikkiError(Exception):
    """Raised when a dump cannot be fetched or does not match the dictionary asked for."""


@dataclass(frozen=True, slots=True)
class KaikkiDict:
    """One dictionary Kaikki can produce: which language file, which bundle."""

    dict_id: str
    language: str  # the Kaikki language name in the URL, e.g. "Hindi"
    lang_code: str  # BCP-47 of the headwords, e.g. "hi"
    name: str

    @property
    def kind(self) -> str:
        return "monolingual" if self.lang_code == "en" else "bilingual"

    def spec(self, version: str) -> DictSpec:
        return DictSpec(
            dict_id=self.dict_id,
            name=self.name,
            source_lang=self.lang_code,
            target_lang="en",
            version=version,
            publisher=_PUBLISHER,
            license=_LICENSE,
            license_url=_LICENSE_URL,
            attribution=_ATTRIBUTION,
            kind=self.kind,
        )


# The languages built, one row each: `dict_id  language  lang_code  senses`. Generated
# by `--discover` (DOCS/sources.md "Wiktionary languages") and checked in, so a build
# never depends on what kaikki.org lists that day.
LANGUAGES_FILE = Path(__file__).resolve().parents[2] / "kaikki-languages.tsv"
_LANGUAGES_HEADER = ("dict_id", "language", "lang_code", "senses")

# Owner decision 2026-09-28: every language with at least this many senses, historical
# languages included, Translingual (symbols and scientific names) left out.
MIN_SENSES = 1000
_EXCLUDED_LANGUAGES = frozenset(
    {
        "Translingual",
        "All languages combined",
        # Wiktionary files these varieties' words under "Chinese" (wikt-zh-en); their own
        # dumps are romanizations and redirects (7 to 207 entries, DOCS/sources.md).
        "Mandarin",
        "Cantonese",
        "Hokkien",
    }
)


def dict_id_for(lang_code: str) -> str:
    """`wikt-en` for English itself, `wikt-<code>-en` for every other language."""
    return "wikt-en" if lang_code == "en" else f"wikt-{lang_code}-en"


def dictionary_for(language: str, lang_code: str) -> KaikkiDict:
    name = "English (Wiktionary)" if lang_code == "en" else f"{language}-English (Wiktionary)"
    return KaikkiDict(dict_id_for(lang_code), language, lang_code, name)


def read_languages(path: Path = LANGUAGES_FILE) -> list[KaikkiDict]:
    """The dictionaries listed in the languages file, in file order."""
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or tuple(lines[0].split("\t")) != _LANGUAGES_HEADER:
        expected = "\t".join(_LANGUAGES_HEADER)
        raise KaikkiError(f"{path}: expected the header {expected!r}")
    dictionaries: list[KaikkiDict] = []
    for number, line in enumerate(lines[1:], start=2):
        fields = line.split("\t")
        if len(fields) != len(_LANGUAGES_HEADER):
            raise KaikkiError(f"{path}:{number}: expected {len(_LANGUAGES_HEADER)} columns")
        dictionary = dictionary_for(fields[1], fields[2])
        if dictionary.dict_id != fields[0]:
            raise KaikkiError(f"{path}:{number}: {fields[0]!r} should be {dictionary.dict_id!r}")
        dictionaries.append(dictionary)
    return dictionaries


DICTIONARIES: Sequence[KaikkiDict] = read_languages()


def find_dictionary(dict_id: str) -> KaikkiDict:
    for candidate in DICTIONARIES:
        if candidate.dict_id == dict_id:
            return candidate
    known = ", ".join(d.dict_id for d in DICTIONARIES)
    raise KaikkiError(f"unknown dictionary {dict_id!r}; kaikki can build: {known}")


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
    def from_json(obj: Json) -> DumpInfo:
        last_modified = obj.get("last_modified")
        return DumpInfo(
            url=str(obj["url"]),
            last_modified=str(last_modified) if last_modified is not None else None,
            dump_date=date.fromisoformat(str(obj["dump_date"])),
            fetched_at=str(obj["fetched_at"]),
        )


def file_stem(language: str) -> str:
    """Kaikki's file name for a language: its name without spaces or punctuation."""
    return _NON_WORD_RE.sub("", language)


def dump_url(language: str) -> str:
    folder = urllib.parse.quote(language)
    stem = urllib.parse.quote(file_stem(language))
    return f"{KAIKKI_BASE_URL}/{folder}/kaikki.org-dictionary-{stem}.jsonl.gz"


def dump_path(cache_dir: Path, language: str) -> Path:
    return cache_dir / f"{file_stem(language)}.jsonl.gz"


def read_dump_info(dump: Path) -> DumpInfo:
    info_path = dump.with_name(dump.name + ".json")
    if not info_path.exists():
        raise KaikkiError(f"{info_path} is missing: fetch the dump first")
    obj = json.loads(info_path.read_text(encoding="utf-8"))
    if not isinstance(obj, Mapping):
        raise KaikkiError(f"{info_path} is not a JSON object")
    return DumpInfo.from_json(obj)


def fetch_dump(cache_dir: Path, language: str, *, timeout: float = 300.0) -> Path:
    """Download the language's dump into `cache_dir` unless the copy there is current.

    Sends If-Modified-Since when a previous copy exists, so a refresh that
    finds nothing new costs one request.
    """
    cache_dir.mkdir(parents=True, exist_ok=True)
    target = dump_path(cache_dir, language)
    info_path = target.with_name(target.name + ".json")
    url = dump_url(language)
    headers = {"User-Agent": USER_AGENT}
    previous = read_dump_info(target) if target.exists() and info_path.exists() else None
    if previous is not None and previous.last_modified:
        headers["If-Modified-Since"] = previous.last_modified

    partial = target.with_name(target.name + ".part")
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            last_modified = response.headers.get("Last-Modified")
            with partial.open("wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)
    except urllib.error.HTTPError as exc:
        if exc.code == 304 and previous is not None:
            logger.info("%s is current (%s)", target, previous.last_modified)
            return target
        raise KaikkiError(f"cannot fetch {url}: HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise KaikkiError(f"cannot fetch {url}: {exc.reason}") from exc

    partial.replace(target)
    fetched_at = datetime.now(UTC).replace(microsecond=0)
    dump_date = (
        parsedate_to_datetime(last_modified).astimezone(UTC).date()
        if last_modified
        else fetched_at.date()
    )
    info = DumpInfo(url, last_modified, dump_date, fetched_at.strftime("%Y-%m-%dT%H:%M:%SZ"))
    info_path.write_text(json.dumps(info.to_json(), indent=2) + "\n", encoding="utf-8")
    logger.info("fetched %s (%s)", target, last_modified)
    return target


# ---------------------------------------------------------------------------
# Discovery: which languages kaikki.org has, and each one's Wiktionary code.
# ---------------------------------------------------------------------------

_INDEX_ROW_RE = re.compile(r'<a href="[^"]+/index\.html">([^<]+?) \((\d+) senses\)</a>')
# Enough of a dump's start to hold its first record once decompressed.
_FIRST_RECORD_BYTES = 128 * 1024


@dataclass(frozen=True, slots=True)
class IndexedLanguage:
    language: str
    senses: int


def parse_index(page: str) -> list[IndexedLanguage]:
    """The languages on kaikki.org's dictionary index page, with their sense counts."""
    return [
        IndexedLanguage(html.unescape(name), int(count))
        for name, count in _INDEX_ROW_RE.findall(page)
    ]


def eligible_languages(
    indexed: Iterable[IndexedLanguage], *, min_senses: int, include_reconstructed: bool
) -> list[IndexedLanguage]:
    """The languages to build: big enough, not excluded, reconstructed ones on request."""
    return [
        entry
        for entry in indexed
        if entry.senses >= min_senses
        and entry.language not in _EXCLUDED_LANGUAGES
        and (include_reconstructed or not entry.language.startswith("Proto-"))
    ]


def _get(url: str, *, timeout: float, byte_range: int | None = None) -> bytes:
    headers = {"User-Agent": USER_AGENT}
    if byte_range is not None:
        headers["Range"] = f"bytes=0-{byte_range - 1}"
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return bytes(response.read())
    except urllib.error.HTTPError as exc:
        raise KaikkiError(f"cannot fetch {url}: HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise KaikkiError(f"cannot fetch {url}: {exc.reason}") from exc


def first_record_code(language: str, *, timeout: float = 60.0) -> str:
    """The Wiktionary language code of a dump, read from its first record only."""
    url = dump_url(language)
    head = _get(url, timeout=timeout, byte_range=_FIRST_RECORD_BYTES)
    try:
        text = zlib.decompressobj(zlib.MAX_WBITS | 16).decompress(head).decode("utf-8", "ignore")
        record = json.loads(text.split("\n", 1)[0])
    except (zlib.error, json.JSONDecodeError) as exc:
        raise KaikkiError(f"{url}: cannot read the first record") from exc
    code = record.get("lang_code") if isinstance(record, dict) else None
    if not isinstance(code, str) or record.get("lang") != language:
        raise KaikkiError(f"{url}: the first record is not a {language} word")
    return code


def discover(
    *, min_senses: int = MIN_SENSES, include_reconstructed: bool = False, timeout: float = 60.0
) -> list[tuple[KaikkiDict, int]]:
    """Every eligible language on kaikki.org as a dictionary, with its sense count."""
    page = _get(f"{KAIKKI_BASE_URL}/", timeout=timeout).decode("utf-8")
    found: list[tuple[KaikkiDict, int]] = []
    for entry in eligible_languages(
        parse_index(page), min_senses=min_senses, include_reconstructed=include_reconstructed
    ):
        code = first_record_code(entry.language, timeout=timeout)
        found.append((dictionary_for(entry.language, code), entry.senses))
        logger.info("%s: %s, %d senses", entry.language, code, entry.senses)
    return found


def write_languages(found: Iterable[tuple[KaikkiDict, int]], path: Path = LANGUAGES_FILE) -> None:
    lines = ["\t".join(_LANGUAGES_HEADER)]
    lines += [f"{d.dict_id}\t{d.language}\t{d.lang_code}\t{senses}" for d, senses in found]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def bundle_version(dump_date: date, build: int = 1) -> str:
    """`YYYY.MM.N`: the month of the Wiktionary dump and a rebuild counter."""
    return f"{dump_date.year}.{dump_date.month:02d}.{build}"


# ---------------------------------------------------------------------------
# JSON access. Kaikki data is loosely typed; these keep the rest honest.
# ---------------------------------------------------------------------------


def _str(obj: Json, key: str) -> str:
    value = obj.get(key)
    return value if isinstance(value, str) else ""


def _objects(obj: Json, key: str) -> list[Json]:
    value = obj.get(key)
    if not isinstance(value, list):
        return []
    return [item for item in value if isinstance(item, Mapping)]


def _strings(obj: Json, key: str) -> list[str]:
    value = obj.get(key)
    if not isinstance(value, list):
        return []
    return [item for item in value if isinstance(item, str)]


def _offsets(obj: Json, key: str) -> list[tuple[int, int]]:
    value = obj.get(key)
    if not isinstance(value, list):
        return []
    spans: list[tuple[int, int]] = []
    for item in value:
        if (
            isinstance(item, list)
            and len(item) == 2
            and all(isinstance(n, int) and not isinstance(n, bool) for n in item)
        ):
            spans.append((item[0], item[1]))
    return spans


def _links(obj: Json) -> list[tuple[str, str]]:
    value = obj.get("links")
    if not isinstance(value, list):
        return []
    pairs: list[tuple[str, str]] = []
    for item in value:
        if isinstance(item, list) and len(item) >= 2:
            text, target = item[0], item[1]
            if isinstance(text, str) and isinstance(target, str):
                pairs.append((text, target))
    return pairs


# ---------------------------------------------------------------------------
# Markup. Everything emitted here is inside the restricted subset (PLAN 4.3).
# ---------------------------------------------------------------------------


def _esc(text: str) -> str:
    return html.escape(text, quote=False)


def _link_target(target: str) -> str:
    """A wiki link target as a headword, or "" when it is not a plain entry."""
    page = target.split("#", 1)[0].strip()
    if not page or ":" in page:
        return ""
    return page


def _is_whole_word(text: str, start: int, end: int) -> bool:
    before = text[start - 1] if start > 0 else ""
    after = text[end] if end < len(text) else ""
    return not (_WORD_CHAR_RE.match(before) or _WORD_CHAR_RE.match(after))


def _render(text: str, spans: Iterable[tuple[int, int, str, str]]) -> str:
    """Escapes `text`, wrapping each (start, end, open, close) span in tags.
    Spans must not overlap; any that do, or fall outside the text, are dropped."""
    parts: list[str] = []
    position = 0
    for start, end, open_tag, close_tag in sorted(spans):
        if start < position or end > len(text) or start >= end:
            continue
        parts.append(_esc(text[position:start]))
        parts.append(open_tag + _esc(text[start:end]) + close_tag)
        position = end
    parts.append(_esc(text[position:]))
    return "".join(parts)


def gloss_html(gloss: str, links: Sequence[tuple[str, str]], headword: str) -> str:
    """The gloss with its wiki links as `lex:` links, first occurrence of each."""
    spans: list[tuple[int, int, str, str]] = []
    taken: list[tuple[int, int]] = []
    for text, raw_target in links:
        target = _link_target(raw_target)
        if not text or not target or target == headword:
            continue
        start = gloss.find(text)
        while start != -1:
            end = start + len(text)
            overlaps = any(start < t_end and t_start < end for t_start, t_end in taken)
            if not overlaps and _is_whole_word(gloss, start, end):
                href = html.escape("lex:" + target, quote=True)
                spans.append((start, end, f'<a href="{href}">', "</a>"))
                taken.append((start, end))
                break
            start = gloss.find(text, start + 1)
    return _render(gloss, spans)


def _bold(text: str, offsets: Sequence[tuple[int, int]]) -> str:
    return _render(text, [(start, end, "<b>", "</b>") for start, end in offsets])


# ---------------------------------------------------------------------------
# Record conversion
# ---------------------------------------------------------------------------


def map_pos(kaikki_pos: str, unknown: Counter[str] | None = None) -> str:
    pos = _POS_MAP.get(kaikki_pos, "other")
    if pos == "other" and unknown is not None:
        unknown[kaikki_pos] += 1
    assert pos in POS_TAGS
    return pos


def _joined(tags: Iterable[str]) -> str | None:
    words = [tag.replace("-", " ") for tag in tags]
    return ", ".join(words) if words else None


def _is_form_of(sense: Json) -> bool:
    return "form-of" in _strings(sense, "tags")


def _definition(sense: Json, headword: str) -> str:
    glosses = [g.strip() for g in _strings(sense, "glosses") if g.strip()]
    if not glosses:
        return ""
    gloss = glosses[-1]
    # "inflection of X:" + "oblique plural": the parent only makes sense together.
    if len(glosses) > 1 and glosses[-2].endswith(":"):
        gloss = f"{glosses[-2]} {gloss}"
    return gloss_html(gloss, _links(sense), headword)


def _examples(sense: Json) -> tuple[Example, ...]:
    candidates = sorted(
        _objects(sense, "examples"), key=lambda ex: 0 if _str(ex, "type") == "example" else 1
    )
    examples: list[Example] = []
    for example in candidates:
        text = _str(example, "text").strip()
        if not text or len(text) > _MAX_EXAMPLE_CHARS:
            continue
        rendered = _bold(text, _offsets(example, "bold_text_offsets"))
        roman = _str(example, "roman").strip()
        if roman:
            rendered += f"<br><i>{_esc(roman)}</i>"
        translation_text = (_str(example, "english") or _str(example, "translation")).strip()
        translation = (
            _bold(translation_text, _offsets(example, "bold_translation_offsets"))
            if translation_text
            else None
        )
        examples.append(Example(text=rendered, translation=translation))
        if len(examples) == _MAX_EXAMPLES_PER_SENSE:
            break
    return tuple(examples)


def _sense(sense: Json, pos: str, proper_noun: bool, headword: str) -> Sense | None:
    tags = _strings(sense, "tags")
    if "no-gloss" in tags:
        return None
    definition = _definition(sense, headword)
    if not definition:
        return None
    grammar = [tag for tag in _GRAMMAR_TAGS if tag in tags]
    if proper_noun:
        grammar.insert(0, "proper-noun")
    usage = [tag for tag in tags if tag in _USAGE_TAGS]
    return Sense(
        definition=definition,
        pos=pos,
        pattern=_joined(grammar),
        label=_joined(usage),
        examples=_examples(sense),
    )


def _pronunciations(record: Json) -> list[Pronunciation]:
    sounds = [s for s in _objects(record, "sounds") if _str(s, "ipa").strip()]
    # Phonemic transcriptions (/.../) read better than phonetic ones ([...]).
    sounds.sort(key=lambda s: 0 if _str(s, "ipa").startswith("/") else 1)
    result: list[Pronunciation] = []
    for sound in sounds:
        region = next((_REGION_TAGS[t] for t in _strings(sound, "tags") if t in _REGION_TAGS), None)
        pron = Pronunciation(ipa=_str(sound, "ipa").strip(), region=region)
        if pron not in result:
            result.append(pron)
    return result


def _forms(record: Json, headword: str) -> list[Form]:
    forms: list[Form] = []
    for item in _objects(record, "forms"):
        text = _str(item, "form").strip()
        tags = _strings(item, "tags")
        if (
            not text
            or text in (headword, "-")
            or _NOISE_FORM_TAGS.intersection(tags)
            or any(tag.startswith("error") for tag in tags)
        ):
            continue
        tag = "romanization" if "romanization" in tags else _joined(tags[:4])
        forms.append(Form(form=text, tag=tag))
    return forms


def _relation_words(obj: Json, key: str, headword: str) -> list[str]:
    words: list[str] = []
    for item in _objects(obj, key):
        word = _str(item, "word").strip()
        if word and word != headword and ":" not in word and word not in words:
            words.append(word)
    return words


def _entry_relations(record: Json, headword: str) -> list[Relation]:
    relations: list[Relation] = []
    for key, rel_type, limit in (
        ("synonyms", "synonym", _MAX_NYMS),
        ("antonyms", "antonym", _MAX_NYMS),
        ("derived", "derived", _MAX_DERIVED),
        ("related", "see", _MAX_RELATED),
    ):
        relations.extend(
            Relation(type=rel_type, target=w)
            for w in _relation_words(record, key, headword)[:limit]
        )
    return relations


def _sense_relations(sense: Json, headword: str, ordinal: int) -> list[Relation]:
    relations: list[Relation] = []
    for key, rel_type in (("synonyms", "synonym"), ("antonyms", "antonym")):
        relations.extend(
            Relation(type=rel_type, target=w, sense_ordinal=ordinal)
            for w in _relation_words(sense, key, headword)[:_MAX_NYMS]
        )
    return relations


@dataclass(slots=True)
class ConversionStats:
    records: int = 0
    entries: int = 0
    skipped_form_of: int = 0
    skipped_other: int = 0
    unknown_pos: Counter[str] = field(default_factory=Counter)


@dataclass(slots=True)
class _Accumulator:
    """The entry being assembled from consecutive records for one word."""

    headword: str
    lang: str
    senses: list[Sense] = field(default_factory=list)
    pronunciations: list[Pronunciation] = field(default_factory=list)
    forms: list[Form] = field(default_factory=list)
    relations: list[Relation] = field(default_factory=list)

    def add(self, record: Json, pos: str, stats: ConversionStats) -> None:
        proper_noun = _str(record, "pos") == "name"
        for sense_json in _objects(record, "senses"):
            sense = _sense(sense_json, pos, proper_noun, self.headword)
            if sense is None:
                continue
            self.senses.append(sense)
            self.relations.extend(_sense_relations(sense_json, self.headword, len(self.senses)))
        for pron in _pronunciations(record):
            if pron not in self.pronunciations:
                self.pronunciations.append(pron)
        known_forms = {form.form for form in self.forms}
        for form in _forms(record, self.headword):
            if form.form not in known_forms:
                self.forms.append(form)
                known_forms.add(form.form)
        known_relations = set(self.relations)
        for relation in _entry_relations(record, self.headword):
            if relation not in known_relations:
                self.relations.append(relation)
                known_relations.add(relation)

    def entry(self) -> Entry | None:
        if not self.senses:
            return None
        return Entry(
            headword=self.headword,
            lang=self.lang,
            senses=tuple(self.senses),
            pronunciations=tuple(self.pronunciations[:_MAX_PRONUNCIATIONS]),
            forms=tuple(self.forms[:_MAX_FORMS]),
            relations=tuple(self.relations),
        )


def convert_records(
    records: Iterable[Json],
    lang_code: str,
    *,
    include_form_of: bool = False,
    stats: ConversionStats | None = None,
) -> Iterator[Entry]:
    """Canonical entries from Kaikki records of one language, in source order."""
    stats = stats if stats is not None else ConversionStats()
    current: _Accumulator | None = None
    for record in records:
        stats.records += 1
        word = _str(record, "word").strip()
        kaikki_pos = _str(record, "pos")
        if not word or kaikki_pos in _SKIP_POS or _str(record, "lang_code") != lang_code:
            stats.skipped_other += 1
            continue
        senses = _objects(record, "senses")
        if not include_form_of and senses and all(_is_form_of(s) for s in senses):
            stats.skipped_form_of += 1
            continue
        if current is None or current.headword != word:
            if current is not None and (done := current.entry()) is not None:
                stats.entries += 1
                yield done
            current = _Accumulator(headword=word, lang=lang_code)
        current.add(record, map_pos(kaikki_pos, stats.unknown_pos), stats)
    if current is not None and (done := current.entry()) is not None:
        stats.entries += 1
        yield done


def read_records(dump: Path) -> Iterator[Json]:
    """Records of a gzipped (or plain) Kaikki JSONL dump, streamed."""
    opener = gzip.open if dump.suffix == ".gz" else open
    with opener(dump, "rt", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            if not line.strip():
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError as exc:
                raise KaikkiError(f"{dump}:{line_number}: invalid JSON: {exc}") from exc
            if isinstance(obj, Mapping):
                yield obj


class KaikkiConverter(Converter):
    """The `Converter` contract over Kaikki's per-language dumps in one cache directory."""

    source_id: ClassVar[str] = "kaikki"

    def __init__(self, cache_dir: Path, *, include_form_of: bool = False) -> None:
        self.cache_dir = cache_dir
        self.include_form_of = include_form_of
        self.stats = ConversionStats()

    def fetch(self, cache_dir: Path) -> Path:
        for dictionary in DICTIONARIES:
            fetch_dump(cache_dir, dictionary.language)
        return cache_dir

    def dictionaries(self) -> list[DictSpec]:
        specs: list[DictSpec] = []
        for dictionary in DICTIONARIES:
            dump = dump_path(self.cache_dir, dictionary.language)
            if dump.exists():
                specs.append(dictionary.spec(bundle_version(read_dump_info(dump).dump_date)))
        return specs

    def iter_records(self, spec: DictSpec) -> Iterator[Entry]:
        dictionary = find_dictionary(spec.dict_id)
        dump = dump_path(self.cache_dir, dictionary.language)
        return convert_records(
            read_records(dump),
            dictionary.lang_code,
            include_form_of=self.include_form_of,
            stats=self.stats,
        )


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.converters.kaikki")
    parser.add_argument("--dict", dest="dict_id", help="dictionary to build, e.g. wikt-hi-en")
    parser.add_argument(
        "--out", type=Path, default=Path("out"), help="bundles go to OUT/<dict_id>/"
    )
    parser.add_argument("--cache", type=Path, default=Path("sources/kaikki"))
    parser.add_argument("--no-fetch", action="store_true", help="use the cached dump as is")
    parser.add_argument("--fetch-only", action="store_true", help="refresh the dump, build nothing")
    parser.add_argument("--limit", type=int, default=None, help="stop after N entries (testing)")
    parser.add_argument("--build-number", type=int, default=1, help="N in version YYYY.MM.N")
    parser.add_argument("--include-form-of", action="store_true")
    parser.add_argument("--built-at", default=None, help="fixed build time, YYYY-MM-DDTHH:MM:SSZ")
    parser.add_argument(
        "--no-vacuum",
        action="store_true",
        help="skip the final VACUUM (it needs a temporary copy of the whole bundle)",
    )
    parser.add_argument("--list", action="store_true", help="list the dictionaries and exit")
    parser.add_argument("--ids-only", action="store_true", help="with --list, print only dict_ids")
    parser.add_argument(
        "--discover",
        action="store_true",
        help=f"rewrite {LANGUAGES_FILE.name} from kaikki.org's current language index",
    )
    parser.add_argument("--min-senses", type=int, default=MIN_SENSES, help="with --discover")
    parser.add_argument(
        "--include-reconstructed",
        action="store_true",
        help="with --discover, keep reconstructed (Proto-) languages",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """CLI: fetch a language's dump if needed, convert it, build its bundle."""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    if args.discover:
        try:
            found = discover(
                min_senses=args.min_senses, include_reconstructed=args.include_reconstructed
            )
        except KaikkiError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1
        write_languages(found)
        print(f"wrote {len(found)} languages to {LANGUAGES_FILE}")
        return 0
    if args.list:
        for dictionary in DICTIONARIES:
            if args.ids_only:
                print(dictionary.dict_id)
            else:
                print(f"{dictionary.dict_id}\t{dictionary.name}\t{dump_url(dictionary.language)}")
        return 0
    if not args.dict_id:
        print("error: --dict is required (see --list)", file=sys.stderr)
        return 1

    try:
        dictionary = find_dictionary(args.dict_id)
        dump = dump_path(args.cache, dictionary.language)
        if not (args.no_fetch and dump.exists()):
            fetch_dump(args.cache, dictionary.language)
        if args.fetch_only:
            return 0
        info = read_dump_info(dump)
        spec = dictionary.spec(bundle_version(info.dump_date, args.build_number))
        converter = KaikkiConverter(args.cache, include_form_of=args.include_form_of)
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
                "source_converter": KaikkiConverter.source_id,
                "source_dump_date": info.dump_date.isoformat(),
            },
            vacuum=not args.no_vacuum,
        )
    except (KaikkiError, BuildError, SchemaError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    stats = converter.stats
    print(
        f"built {out_path}: {stats.entries} entries from {stats.records} records "
        f"({stats.skipped_form_of} inflection-only and {stats.skipped_other} other records skipped)"
    )
    if stats.unknown_pos:
        print(f"parts of speech mapped to 'other': {dict(stats.unknown_pos.most_common())}")
    return 0


__all__ = [
    "DICTIONARIES",
    "LANGUAGES_FILE",
    "ConversionStats",
    "DumpInfo",
    "IndexedLanguage",
    "KaikkiConverter",
    "KaikkiDict",
    "KaikkiError",
    "bundle_version",
    "convert_records",
    "dict_id_for",
    "dictionary_for",
    "discover",
    "eligible_languages",
    "fetch_dump",
    "file_stem",
    "first_record_code",
    "gloss_html",
    "map_pos",
    "parse_index",
    "read_languages",
    "read_records",
    "write_languages",
]

if __name__ == "__main__":
    raise SystemExit(main())
