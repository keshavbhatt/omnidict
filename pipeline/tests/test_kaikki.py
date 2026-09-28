"""Kaikki converter: record mapping, grouping, fetching and a full bundle build.

The records here are hand-written in Kaikki's shape; no Wiktionary text is copied.
"""

from __future__ import annotations

import gzip
import io
import json
import sqlite3
import urllib.error
import urllib.request
from collections.abc import Iterator, Mapping, Sequence
from datetime import date
from email.message import Message
from pathlib import Path

import pytest

from omnipipe.converters import kaikki
from omnipipe.converters.kaikki import (
    ConversionStats,
    KaikkiConverter,
    KaikkiError,
    bundle_version,
    convert_records,
    fetch_dump,
    gloss_html,
    map_pos,
    read_dump_info,
)
from omnipipe.html_subset import validate
from omnipipe.schema import Entry

Record = dict[str, object]


def record(word: str, pos: str, senses: Sequence[Record], **extra: object) -> Record:
    return {"word": word, "pos": pos, "lang_code": "xx", "senses": list(senses), **extra}


def sense(*glosses: str, **extra: object) -> Record:
    return {"glosses": list(glosses), **extra}


def convert(records: Sequence[Record], *, include_form_of: bool = False) -> list[Entry]:
    return list(convert_records(records, "xx", include_form_of=include_form_of))


def all_markup(entry: Entry) -> list[str]:
    texts: list[str] = []
    for s in entry.senses:
        texts.append(s.definition)
        for example in s.examples:
            texts.append(example.text)
            if example.translation is not None:
                texts.append(example.translation)
    return texts


# --- markup -----------------------------------------------------------------


def test_gloss_links_become_lex_links_on_whole_words_only() -> None:
    html = gloss_html(
        "a cat, not a catalog",
        [("cat", "cat#English"), ("catalog", "catalog")],
        "gato",
    )
    assert html == 'a <a href="lex:cat">cat</a>, not a <a href="lex:catalog">catalog</a>'


def test_gloss_links_skip_namespaces_self_links_and_missing_text() -> None:
    html = gloss_html(
        "a small pond",
        [("pond", "w:Pond"), ("small", "tiny"), ("lake", "lake"), ("small", "small#X")],
        "tiny",
    )
    # "small" -> "tiny" is the headword itself; the second pair still links it.
    assert html == 'a <a href="lex:small">small</a> pond'


def test_gloss_text_is_escaped() -> None:
    html = gloss_html("less than <x> & more", [("more", "more")], "h")
    assert html == 'less than &lt;x&gt; &amp; <a href="lex:more">more</a>'
    assert validate(html) == ()


def test_pos_mapping_and_unknown_counting() -> None:
    from collections import Counter

    unknown: Counter[str] = Counter()
    assert map_pos("intj", unknown) == "interj"
    assert map_pos("postp", unknown) == "prep"
    assert map_pos("name", unknown) == "noun"
    assert map_pos("character", unknown) == "other"
    assert unknown == Counter({"character": 1})


# --- record conversion ------------------------------------------------------


def test_consecutive_records_for_one_word_merge_into_one_entry() -> None:
    entries = convert(
        [
            record("lipo", "noun", [sense("a hill")]),
            record("lipo", "verb", [sense("to climb")]),
            record("mafa", "adj", [sense("red")]),
        ]
    )
    assert [e.headword for e in entries] == ["lipo", "mafa"]
    assert [(s.pos, s.definition) for s in entries[0].senses] == [
        ("noun", "a hill"),
        ("verb", "to climb"),
    ]


def test_inflection_only_records_are_skipped_unless_asked_for() -> None:
    records = [
        record("lipos", "noun", [sense("plural of lipo", tags=["form-of", "plural"])]),
        record("lipo", "noun", [sense("a hill")]),
    ]
    stats = ConversionStats()
    entries = list(convert_records(records, "xx", stats=stats))
    assert [e.headword for e in entries] == ["lipo"]
    assert stats.skipped_form_of == 1
    assert [e.headword for e in convert(records, include_form_of=True)] == ["lipos", "lipo"]


def test_records_of_another_language_or_romanizations_are_skipped() -> None:
    stats = ConversionStats()
    records = [
        {**record("lipo", "noun", [sense("a hill")]), "lang_code": "yy"},
        record("lipo", "romanization", [sense("romanization of X")]),
        record("mafa", "adj", [sense("red")]),
    ]
    entries = list(convert_records(records, "xx", stats=stats))
    assert [e.headword for e in entries] == ["mafa"]
    assert stats.skipped_other == 2
    assert stats.records == 3


def test_senses_without_glosses_are_dropped_and_empty_entries_vanish() -> None:
    entries = convert(
        [
            record("lipo", "noun", [sense(), sense("x", tags=["no-gloss"])]),
            record("mafa", "adj", [sense(), sense("red")]),
        ]
    )
    assert [(e.headword, len(e.senses)) for e in entries] == [("mafa", 1)]


def test_nested_glosses_use_the_innermost_unless_the_parent_introduces_it() -> None:
    entries = convert(
        [
            record(
                "lipo",
                "noun",
                [
                    sense("a landform", "a small hill"),
                    sense("inflection of lipa:", "oblique singular"),
                ],
            )
        ]
    )
    assert [s.definition for s in entries[0].senses] == [
        "a small hill",
        "inflection of lipa: oblique singular",
    ]


def test_grammar_and_usage_tags() -> None:
    entries = convert(
        [
            record(
                "lipo",
                "verb",
                [sense("to climb", tags=["transitive", "masculine", "slang", "archaic", "past"])],
            ),
            record("Mafa", "name", [sense("a town")]),
        ]
    )
    climb = entries[0].senses[0]
    assert climb.pattern == "masculine, transitive"
    assert climb.label == "slang, archaic"
    town = entries[1].senses[0]
    assert (town.pos, town.pattern, town.label) == ("noun", "proper noun", None)


def test_examples_prefer_short_examples_with_bold_romanization_and_translation() -> None:
    examples = [
        {"text": "x" * 400, "type": "quotation"},
        {"text": "a quoted line", "type": "quotation"},
        {
            "text": "lipo mafa",
            "type": "example",
            "bold_text_offsets": [[0, 4]],
            "roman": "lipo mafa",
            "english": "a red hill",
            "bold_translation_offsets": [[6, 10]],
        },
    ]
    entries = convert([record("lipo", "noun", [sense("a hill", examples=examples)])])
    rendered = entries[0].senses[0].examples
    assert [(e.text, e.translation) for e in rendered] == [
        ("<b>lipo</b> mafa<br><i>lipo mafa</i>", "a red <b>hill</b>"),
        ("a quoted line", None),
    ]


def test_example_count_is_capped() -> None:
    examples = [{"text": f"line {n}", "type": "example"} for n in range(6)]
    entries = convert([record("lipo", "noun", [sense("a hill", examples=examples)])])
    assert len(entries[0].senses[0].examples) == 3


def test_forms_drop_table_noise_duplicates_and_the_headword() -> None:
    forms = [
        {"form": "lipo-ka", "tags": ["romanization"]},
        {"form": "no-table-tags", "tags": ["table-tags"]},
        {"form": "xx-decl", "tags": ["inflection-template"]},
        {"form": "lipo", "tags": ["singular"]},
        {"form": "lipos", "tags": ["plural"]},
        {"form": "lipos", "tags": ["plural", "direct"]},
        {"form": "?", "tags": ["error-unrecognized-form"]},
    ]
    entries = convert([record("lipo", "noun", [sense("a hill")], forms=forms)])
    assert [(f.form, f.tag) for f in entries[0].forms] == [
        ("lipo-ka", "romanization"),
        ("lipos", "plural"),
    ]


def test_pronunciations_prefer_phonemic_and_map_regions() -> None:
    sounds = [
        {"ipa": "[li.po]", "tags": ["US"]},
        {"ipa": "/\u02c8li.po/", "tags": ["Received-Pronunciation"]},
        {"ipa": "/\u02c8li.po/", "tags": ["Received-Pronunciation"]},
        {"audio": "x.ogg"},
    ]
    entries = convert([record("lipo", "noun", [sense("a hill")], sounds=sounds)])
    assert [(p.ipa, p.region) for p in entries[0].pronunciations] == [
        ("/\u02c8li.po/", "UK"),
        ("[li.po]", "US"),
    ]


def test_relations_from_entries_and_senses() -> None:
    entries = convert(
        [
            record(
                "lipo",
                "noun",
                [
                    sense("a hill", synonyms=[{"word": "mafa"}, {"word": "lipo"}]),
                    sense("a mound", antonyms=[{"word": "dipa"}]),
                ],
                derived=[{"word": "lipolo"}, {"word": "Thesaurus:lipo"}],
                related=[{"word": "lipa"}],
            )
        ]
    )
    assert [(r.type, r.target, r.sense_ordinal) for r in entries[0].relations] == [
        ("synonym", "mafa", 1),
        ("antonym", "dipa", 2),
        ("derived", "lipolo", None),
        ("see", "lipa", None),
    ]


def test_every_emitted_markup_passes_the_subset_validator() -> None:
    entries = convert(
        [
            record(
                "lipo",
                "noun",
                [
                    sense(
                        "a <b>bold</b> & risky hill",
                        links=[["hill", "hill"], ["risky", "risky"]],
                        examples=[{"text": "1 < 2", "english": "x & y", "type": "example"}],
                    )
                ],
            )
        ]
    )
    for text in all_markup(entries[0]):
        assert validate(text) == (), text


# --- dump handling ------------------------------------------------------------


def write_dump(path: Path, records: Sequence[Mapping[str, object]]) -> None:
    with gzip.open(path, "wt", encoding="utf-8") as handle:
        for item in records:
            handle.write(json.dumps(item, ensure_ascii=False) + "\n")


def test_bundle_version_uses_the_dump_month() -> None:
    assert bundle_version(date(2026, 9, 25)) == "2026.09.1"
    assert bundle_version(date(2026, 10, 1), 3) == "2026.10.3"


class FakeResponse(io.BytesIO):
    def __init__(self, body: bytes, last_modified: str) -> None:
        super().__init__(body)
        self.headers = Message()
        self.headers["Last-Modified"] = last_modified

    def __enter__(self) -> FakeResponse:
        return self


def test_fetch_writes_the_dump_and_its_info_then_honours_not_modified(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    requests: list[urllib.request.Request] = []

    def first(request: urllib.request.Request, timeout: float) -> FakeResponse:
        requests.append(request)
        return FakeResponse(b"payload", "Fri, 25 Sep 2026 10:15:23 GMT")

    monkeypatch.setattr(urllib.request, "urlopen", first)
    dump = fetch_dump(tmp_path, "Lipo")
    assert dump.read_bytes() == b"payload"
    info = read_dump_info(dump)
    assert info.dump_date == date(2026, 9, 25)
    assert info.url.endswith("/Lipo/kaikki.org-dictionary-Lipo.jsonl.gz")
    assert requests[0].get_header("User-agent", "").startswith("omnipipe/")

    def not_modified(request: urllib.request.Request, timeout: float) -> FakeResponse:
        requests.append(request)
        raise urllib.error.HTTPError(request.full_url, 304, "Not Modified", Message(), None)

    monkeypatch.setattr(urllib.request, "urlopen", not_modified)
    assert fetch_dump(tmp_path, "Lipo") == dump
    assert requests[1].get_header("If-modified-since") == "Fri, 25 Sep 2026 10:15:23 GMT"
    assert dump.read_bytes() == b"payload"


def test_fetch_failure_is_a_kaikki_error(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    def failing(request: urllib.request.Request, timeout: float) -> FakeResponse:
        raise urllib.error.HTTPError(request.full_url, 404, "Not Found", Message(), None)

    monkeypatch.setattr(urllib.request, "urlopen", failing)
    with pytest.raises(KaikkiError, match="HTTP 404"):
        fetch_dump(tmp_path, "Lipo")
    assert not (tmp_path / "Lipo.jsonl.gz").exists()


@pytest.fixture
def hindi_like_cache(tmp_path: Path) -> Iterator[Path]:
    cache = tmp_path / "cache"
    cache.mkdir()
    dump = cache / "Hindi.jsonl.gz"
    records = [
        {
            "word": "कलम",
            "pos": "noun",
            "lang_code": "hi",
            "forms": [{"form": "kalam", "tags": ["romanization"]}],
            "senses": [{"glosses": ["pen"], "tags": ["feminine"], "links": [["pen", "pen"]]}],
        },
        {
            "word": "कलमें",
            "pos": "noun",
            "lang_code": "hi",
            "senses": [{"glosses": ["plural of कलम"], "tags": ["form-of", "plural"]}],
        },
    ]
    write_dump(dump, records)
    info = {
        "url": "https://example.invalid/Hindi.jsonl.gz",
        "last_modified": "Fri, 25 Sep 2026 10:15:23 GMT",
        "dump_date": "2026-09-25",
        "fetched_at": "2026-09-27T00:00:00Z",
    }
    dump.with_name(dump.name + ".json").write_text(json.dumps(info), encoding="utf-8")
    yield cache


def test_converter_contract_lists_cached_dictionaries(hindi_like_cache: Path) -> None:
    converter = KaikkiConverter(hindi_like_cache)
    specs = converter.dictionaries()
    assert [(s.dict_id, s.version, s.kind) for s in specs] == [
        ("wikt-hi-en", "2026.09.1", "bilingual")
    ]
    entries = list(converter.iter_records(specs[0]))
    assert [e.headword for e in entries] == ["कलम"]
    assert converter.stats.skipped_form_of == 1


def test_cli_builds_a_bundle_with_source_metadata(hindi_like_cache: Path, tmp_path: Path) -> None:
    out = tmp_path / "out"
    code = kaikki.main(
        [
            "--dict",
            "wikt-hi-en",
            "--cache",
            str(hindi_like_cache),
            "--out",
            str(out),
            "--no-fetch",
            "--built-at",
            "2026-09-27T00:00:00Z",
        ]
    )
    assert code == 0
    conn = sqlite3.connect(out / "wikt-hi-en" / "dict.sqlite")
    meta = dict(conn.execute("SELECT key, value FROM meta").fetchall())
    assert meta["source_converter"] == "kaikki"
    assert meta["source_dump_date"] == "2026-09-25"
    assert meta["version"] == "2026.09.1"
    assert meta["license"] == "CC-BY-SA-4.0"
    assert "Wiktionary" in meta["attribution"]
    rows = conn.execute(
        "SELECT e.headword FROM forms f JOIN entries e ON e.id = f.entry_id WHERE f.form_norm = ?",
        ("kalam",),
    ).fetchall()
    assert rows == [("कलम",)]
    conn.close()


def test_cli_rejects_an_unknown_dictionary(capsys: pytest.CaptureFixture[str]) -> None:
    assert kaikki.main(["--dict", "wikt-xx-en", "--no-fetch"]) == 1
    assert "unknown dictionary" in capsys.readouterr().err


# --- the language list -----------------------------------------------------------

INDEX_PAGE = """
<li><a href="All%20languages%20combined/index.html">All languages combined (900 senses)</a></li>
<li><a href="Lipo/index.html">Lipo (5000 senses)</a></li>
<li><a href="Translingual/index.html">Translingual (4000 senses)</a></li>
<li><a href="Old%20Lipo/index.html">Old Lipo (1000 senses)</a></li>
<li><a href="Proto-Lipo/index.html">Proto-Lipo (3000 senses)</a></li>
<li><a href="Ye%27lipo/index.html">Ye&#x27;lipo (999 senses)</a></li>
"""


def test_parse_index_reads_names_and_sense_counts() -> None:
    parsed = kaikki.parse_index(INDEX_PAGE)
    assert [(p.language, p.senses) for p in parsed][-1] == ("Ye'lipo", 999)
    assert len(parsed) == 6


def test_eligible_languages_apply_the_cut_off_and_the_exclusions() -> None:
    parsed = kaikki.parse_index(INDEX_PAGE)
    kept = kaikki.eligible_languages(parsed, min_senses=1000)
    assert [k.language for k in kept] == ["Lipo", "Old Lipo", "Proto-Lipo"]


def test_dump_urls_drop_spaces_and_punctuation_from_the_file_name() -> None:
    assert kaikki.dump_url("Old English").endswith(
        "/Old%20English/kaikki.org-dictionary-OldEnglish.jsonl.gz"
    )
    assert kaikki.dump_url("Franco-Provençal").endswith(
        "/Franco-Proven%C3%A7al/kaikki.org-dictionary-FrancoProven%C3%A7al.jsonl.gz"
    )
    assert kaikki.dump_path(Path("c"), "Ye'kwana") == Path("c/Yekwana.jsonl.gz")


def test_dictionary_ids_and_names_follow_the_language() -> None:
    english = kaikki.dictionary_for("English", "en")
    assert (english.dict_id, english.name, english.kind) == (
        "wikt-en",
        "English (Wiktionary)",
        "monolingual",
    )
    spec = kaikki.dictionary_for("Old English", "ang").spec("2026.09.1")
    assert (spec.source_lang_name, spec.target_lang_name) == ("Old English", "English")
    assert spec.source_url == kaikki.dump_url("Old English")
    old = kaikki.dictionary_for("Old Polish", "zlw-opl")
    assert (old.dict_id, old.name, old.kind) == (
        "wikt-zlw-opl-en",
        "Old Polish-English (Wiktionary)",
        "bilingual",
    )


def test_languages_file_round_trips_and_rejects_a_wrong_id(tmp_path: Path) -> None:
    path = tmp_path / "languages.tsv"
    found = [(kaikki.dictionary_for("Lipo", "lpo"), 5000)]
    kaikki.write_languages(found, path)
    assert kaikki.read_languages(path) == [found[0][0]]
    path.write_text(path.read_text().replace("wikt-lpo-en", "wikt-xx-en"))
    with pytest.raises(KaikkiError, match="should be 'wikt-lpo-en'"):
        kaikki.read_languages(path)


def test_the_checked_in_languages_file_keeps_the_owner_decisions() -> None:
    ids = {d.dict_id for d in kaikki.DICTIONARIES}
    languages = {d.language for d in kaikki.DICTIONARIES}
    assert {"wikt-en", "wikt-es-en", "wikt-hi-en", "wikt-la-en", "wikt-grc-en"} <= ids
    assert "Old English" in languages
    assert "Translingual" not in languages
    assert not {"Mandarin", "Cantonese", "Hokkien"} & languages
    assert "Proto-Germanic" in languages  # reconstructed languages are in (owner, 2026-09-28)
    assert len(ids) == len(kaikki.DICTIONARIES)


def test_first_record_code_reads_only_the_start_of_the_dump(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    first = {"word": "a", "lang": "Old Lipo", "lang_code": "olp"}
    body = gzip.compress((json.dumps(first) + "\n" + "x" * 500_000).encode())
    ranges: list[str | None] = []

    def urlopen(request: urllib.request.Request, timeout: float) -> FakeResponse:
        ranges.append(request.get_header("Range"))
        return FakeResponse(body[:1000], "")

    monkeypatch.setattr(urllib.request, "urlopen", urlopen)
    assert kaikki.first_record_code("Old Lipo") == "olp"
    assert ranges == ["bytes=0-131071"]
    with pytest.raises(KaikkiError, match="not a Lipo word"):
        kaikki.first_record_code("Lipo")


def test_a_leading_asterisk_is_dropped_from_forms() -> None:
    [entry] = convert(
        [record("aba", "adv", [sense("away")], forms=[{"form": "*afa"}, {"form": "*aba"}])]
    )
    assert [f.form for f in entry.forms] == ["afa"]  # "*aba" is the headword itself
