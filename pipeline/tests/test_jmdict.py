"""JMdict converter tests: entity resolution, mapping, and a full bundle build.

The sample XML is hand-written, with a small internal DTD subset covering only
the entity codes this file uses; no JMdict text is copied.
"""

from __future__ import annotations

import gzip
import sqlite3
import xml.etree.ElementTree as ET
from collections import Counter
from datetime import date
from pathlib import Path

import icu

from omnipipe.converters.jmdict import (
    ConversionStats,
    bundle_version,
    convert_entry,
    dictionary_spec,
    entry_frequency,
    iter_entries,
    main,
    map_pos,
    parse_created_date,
    parse_entities,
)
from omnipipe.html_subset import validate
from omnipipe.normalize import normalize_headword
from omnipipe.schema import Entry

SAMPLE_XML = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE JMdict [
<!ENTITY n "noun (common) (futsuumeishi)">
<!ENTITY v1 "Ichidan verb">
<!ENTITY vt "transitive verb">
<!ENTITY adj-na "adjectival nouns or quasi-adjectives (keiyodoshi)">
<!ENTITY int "interjection (kandoushi)">
<!ENTITY uk "word usually written using kana alone">
<!ENTITY col "colloquial">
<!ENTITY food "food, cooking">
]>
<!-- JMdict created: 2026-09-25 -->
<JMdict>
<entry>
<ent_seq>1</ent_seq>
<k_ele><keb>食べる</keb></k_ele>
<r_ele><reb>たべる</reb></r_ele>
<sense><pos>&v1;</pos><pos>&vt;</pos><gloss>to eat</gloss></sense>
</entry>
<entry>
<ent_seq>2</ent_seq>
<k_ele><keb>学校</keb><ke_pri>ichi1</ke_pri><ke_pri>news1</ke_pri></k_ele>
<r_ele><reb>がっこう</reb><re_pri>ichi1</re_pri><re_pri>news1</re_pri></r_ele>
<sense><pos>&n;</pos><gloss>school</gloss></sense>
</entry>
<entry>
<ent_seq>3</ent_seq>
<r_ele><reb>ねこ</reb></r_ele>
<sense><pos>&n;</pos><misc>&uk;</misc><gloss>cat</gloss></sense>
</entry>
<entry>
<ent_seq>4</ent_seq>
<k_ele><keb>御田</keb></k_ele>
<k_ele><keb>お田</keb></k_ele>
<r_ele><reb>おでん</reb><re_pri>spec1</re_pri></r_ele>
<sense><pos>&n;</pos><field>&food;</field><misc>&uk;</misc>
<gloss>oden</gloss><gloss>hotpot dish</gloss></sense>
</entry>
<entry>
<ent_seq>5</ent_seq>
<k_ele><keb>有難う</keb><ke_pri>nf25</ke_pri></k_ele>
<r_ele><reb>ありがとう</reb><re_pri>nf25</re_pri></r_ele>
<sense><pos>&int;</pos><misc>&col;</misc><s_inf>used casually</s_inf>
<gloss>thanks</gloss><gloss>thank you</gloss></sense>
</entry>
<entry>
<ent_seq>6</ent_seq>
<k_ele><keb>アーバン</keb></k_ele>
<r_ele><reb>アーバン</reb></r_ele>
<sense><pos>&adj-na;</pos><pos>&n;</pos><xref>ルーラル</xref><ant>ルーラル</ant>
<gloss>urban</gloss></sense>
</entry>
<entry>
<ent_seq>7</ent_seq>
<k_ele><keb>丸</keb></k_ele>
<r_ele><reb>まる</reb></r_ele>
<sense><pos>&n;</pos><xref>二重丸・にじゅうまる・1</xref><gloss>circle</gloss></sense>
</entry>
</JMdict>
"""


def write_sample(tmp_path: Path) -> Path:
    dump = tmp_path / "JMdict_e.xml"
    dump.write_text(SAMPLE_XML, encoding="utf-8")
    return dump


def header_of(dump: Path) -> str:
    return dump.read_text(encoding="utf-8")


# --- header parsing -----------------------------------------------------------


def test_parse_entities_maps_value_back_to_code(tmp_path: Path) -> None:
    header = header_of(write_sample(tmp_path))
    entities = parse_entities(header)
    assert entities["noun (common) (futsuumeishi)"] == "n"
    assert entities["transitive verb"] == "vt"
    assert entities["word usually written using kana alone"] == "uk"


def test_parse_created_date(tmp_path: Path) -> None:
    header = header_of(write_sample(tmp_path))
    assert parse_created_date(header).isoformat() == "2026-09-25"


def test_bundle_version_uses_the_created_month() -> None:
    assert bundle_version(date(2026, 9, 25)) == "2026.09.1"
    assert bundle_version(date(2026, 10, 1), 3) == "2026.10.3"


# --- pos mapping ----------------------------------------------------------------


def test_pos_mapping_by_prefix_and_unknown_counting() -> None:
    unknown: Counter[str] = Counter()
    assert map_pos("n", unknown) == "noun"
    assert map_pos("v5k", unknown) == "verb"
    assert map_pos("vt", unknown) == "verb"
    assert map_pos("adj-na", unknown) == "adj"
    assert map_pos("adv-to", unknown) == "adv"
    assert map_pos("pn", unknown) == "pron"
    assert map_pos("prt", unknown) == "particle"
    assert map_pos("conj", unknown) == "conj"
    assert map_pos("int", unknown) == "interj"
    assert map_pos("num", unknown) == "num"
    for code in ("ctr", "suf", "pref", "n-pref", "n-suf", "exp", "aux", "aux-v", "cop", "unc"):
        assert map_pos(code, unknown) == "other"
    expected_codes = ("ctr", "suf", "pref", "n-pref", "n-suf", "exp", "aux", "aux-v", "cop", "unc")
    assert unknown == Counter(dict.fromkeys(expected_codes, 1))


# --- frequency ---------------------------------------------------------------


def test_entry_frequency_tiers() -> None:
    assert entry_frequency([]) is None
    assert entry_frequency(["news1", "ichi1"]) == 5
    assert entry_frequency(["news2"]) == 4
    assert entry_frequency(["nf12"]) == 3
    assert entry_frequency(["nf25"]) == 2
    assert entry_frequency(["oK"]) == 1


# --- entry conversion ----------------------------------------------------------


def entries_by_headword(dump: Path) -> dict[str, Entry]:
    header = header_of(dump)
    entities = parse_entities(header)
    return {e.headword: e for e in iter_entries(dump, entities)}


def test_headword_is_first_keb_or_first_reb_when_none(tmp_path: Path) -> None:
    dump = write_sample(tmp_path)
    entries = entries_by_headword(dump)
    assert "食べる" in entries
    assert "ねこ" in entries  # no k_ele: headword falls back to the reading


def test_verb_pos_and_transitivity_pattern(tmp_path: Path) -> None:
    entries = entries_by_headword(write_sample(tmp_path))
    sense = entries["食べる"].senses[0]
    assert sense.pos == "verb"
    assert sense.pattern == "transitive"
    assert sense.definition == "to eat"


def test_multiple_kebs_and_rebs_become_forms(tmp_path: Path) -> None:
    entries = entries_by_headword(write_sample(tmp_path))
    entry = entries["御田"]
    forms = {(f.form, f.tag) for f in entry.forms}
    assert ("お田", "variant") in forms
    assert ("おでん", "reading") in forms
    assert ("oden", "romanization") in forms


def test_romaji_form_matches_normalized_latin_input(tmp_path: Path) -> None:
    entries = entries_by_headword(write_sample(tmp_path))
    entry = entries["食べる"]
    romaji_forms = [f.form for f in entry.forms if f.tag == "romanization"]
    assert romaji_forms == ["taberu"]
    assert normalize_headword("taberu") == normalize_headword(romaji_forms[0])


def test_misc_and_field_labels_are_readable(tmp_path: Path) -> None:
    entries = entries_by_headword(write_sample(tmp_path))
    cat = entries["ねこ"].senses[0]
    assert cat.label == "usually kana"
    oden = entries["御田"].senses[0]
    assert oden.label == "usually kana, food, cooking"
    thanks = entries["有難う"].senses[0]
    assert thanks.pos == "interj"
    assert thanks.label == "colloquial, used casually"


def test_xref_and_ant_become_relations_using_the_headword_segment(tmp_path: Path) -> None:
    entries = entries_by_headword(write_sample(tmp_path))
    urban = entries["アーバン"]
    assert [(r.type, r.target, r.sense_ordinal) for r in urban.relations] == [
        ("see", "ルーラル", 1),
        ("antonym", "ルーラル", 1),
    ]
    circle = entries["丸"]
    assert [(r.type, r.target, r.sense_ordinal) for r in circle.relations] == [
        ("see", "二重丸", 1),
    ]


def test_priority_tags_become_entry_frequency(tmp_path: Path) -> None:
    entries = entries_by_headword(write_sample(tmp_path))
    assert entries["食べる"].frequency is None
    assert entries["学校"].frequency == 5
    assert entries["御田"].frequency == 5
    assert entries["有難う"].frequency == 2


def test_every_emitted_markup_passes_the_subset_validator(tmp_path: Path) -> None:
    for entry in entries_by_headword(write_sample(tmp_path)).values():
        for sense in entry.senses:
            assert validate(sense.definition) == (), sense.definition


def test_no_senses_means_no_entry() -> None:
    stats = ConversionStats()
    elem = ET.fromstring("<entry><r_ele><reb>x</reb></r_ele></entry>")
    transliterator = icu.Transliterator.createInstance("Any-Latin; Latin-ASCII")
    assert convert_entry(elem, {}, transliterator, stats) is None
    assert stats.skipped_no_senses == 1


# --- full bundle build ----------------------------------------------------------


def test_cli_builds_a_bundle_from_the_dump(tmp_path: Path) -> None:
    cache = tmp_path / "cache"
    cache.mkdir()
    dump = cache / "JMdict_e.gz"
    with gzip.open(dump, "wt", encoding="utf-8") as handle:
        handle.write(SAMPLE_XML)
    out = tmp_path / "out"

    code = main(
        [
            "--dict",
            "jmdict-ja-en",
            "--cache",
            str(cache),
            "--out",
            str(out),
            "--no-fetch",
            "--built-at",
            "2026-09-27T00:00:00Z",
        ]
    )
    assert code == 0

    conn = sqlite3.connect(out / "jmdict-ja-en" / "dict.sqlite")
    meta = dict(conn.execute("SELECT key, value FROM meta").fetchall())
    assert meta["entry_count"] == "7"
    assert meta["version"] == "2026.09.1"
    assert meta["license"] == "CC-BY-SA-4.0"
    assert "Electronic Dictionary Research and Development Group" in meta["attribution"]
    assert meta["source_converter"] == "jmdict"
    assert meta["source_dump_date"] == "2026-09-25"

    rows = conn.execute(
        "SELECT e.headword FROM forms f JOIN entries e ON e.id = f.entry_id WHERE f.form_norm = ?",
        ("taberu",),
    ).fetchall()
    assert rows == [("食べる",)]
    conn.close()


def test_dictionary_spec_matches_the_licence(tmp_path: Path) -> None:
    spec = dictionary_spec("2026.09.1")
    assert spec.dict_id == "jmdict-ja-en"
    assert spec.source_lang == "ja"
    assert spec.target_lang == "en"
    assert spec.kind == "bilingual"
    assert spec.license == "CC-BY-SA-4.0"
