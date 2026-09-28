"""Tests for `omnipipe.converters.cedict`.

The sample lines are hand-written in CC-CEDICT's shape; no dictionary text is copied.
"""

from __future__ import annotations

import gzip
import sqlite3
from datetime import date
from pathlib import Path

from omnipipe.build import build_bundle
from omnipipe.converters import cedict
from omnipipe.converters.cedict import (
    CedictConverter,
    ConversionStats,
    bundle_version,
    convert_lines,
    convert_pinyin,
    gloss_html,
    parse_line,
    pinyin_forms,
    read_header_date,
    spec_for,
)
from omnipipe.html_subset import validate
from omnipipe.normalize import normalize_headword
from omnipipe.schema import Entry

# --- pinyin conversion --------------------------------------------------------


def test_pinyin_tone_marks_follow_the_standard_placement_rule() -> None:
    assert convert_pinyin("ni3 hao3") == "nǐ hǎo"
    assert convert_pinyin("Zhong1 guo2") == "Zhōng guó"
    assert convert_pinyin("xue2 xi2") == "xué xí"
    assert convert_pinyin("xie4 xie5") == "xiè xie"  # tone 5 is neutral: no mark


def test_pinyin_handles_u_colon_and_v_as_u_umlaut() -> None:
    assert convert_pinyin("lu:4") == "lǜ"
    assert convert_pinyin("nu:4") == "nǜ"
    assert convert_pinyin("lv4") == "lǜ"


def test_pinyin_tokens_without_a_trailing_tone_digit_pass_through() -> None:
    assert convert_pinyin("11 Qu1") == "11 Qū"
    assert convert_pinyin("w o r d") == "w o r d"


def test_pinyin_forms_gives_a_spaced_and_a_concatenated_form() -> None:
    assert pinyin_forms("ni3 hao3") == ("nǐ hǎo", "nǐhǎo")
    # A single syllable has nothing to concatenate: both forms are identical.
    assert pinyin_forms("san1") == ("sān", "sān")


# --- gloss cross-references ----------------------------------------------------


def test_gloss_cross_reference_becomes_a_lex_link_dropping_the_pinyin() -> None:
    html_out = gloss_html("see 你好[ni3 hao3]")
    assert html_out == 'see <a href="lex:你好">你好</a>'
    assert validate(html_out) == ()


def test_gloss_cross_reference_with_traditional_and_simplified_links_to_simplified() -> None:
    html_out = gloss_html("variant of 開金|开金[kai1 jin1]")
    assert html_out == ('variant of <a href="lex:开金">開金|开金</a>')


def test_gloss_text_without_a_reference_is_only_escaped() -> None:
    assert gloss_html("a < b & c > d") == "a &lt; b &amp; c &gt; d"


# --- line parsing ---------------------------------------------------------------


def test_parse_line_splits_traditional_simplified_pinyin_and_glosses() -> None:
    parsed = parse_line("你好 你好 [ni3 hao3] /hello; hi/")
    assert parsed is not None
    assert parsed.traditional == "你好"
    assert parsed.simplified == "你好"
    assert parsed.pinyin == "ni3 hao3"
    assert parsed.defs == ("hello; hi",)


def test_parse_line_rejects_malformed_lines() -> None:
    assert parse_line("not a cedict line at all") is None
    assert parse_line("你好 你好 ni3 hao3 /hello/") is None  # missing brackets
    assert parse_line("你好 你好 [ni3 hao3] hello") is None  # missing slashes


def test_malformed_and_comment_lines_are_skipped_and_counted() -> None:
    stats = ConversionStats()
    lines = [
        "# CC-CEDICT",
        "#! date=2026-09-27T12:50:23Z",
        "",
        "你好 你好 [ni3 hao3] /hello; hi/",
        "this is not a valid line",
    ]
    entries = list(convert_lines(lines, stats))
    assert [e.headword for e in entries] == ["你好"]
    assert stats.malformed == 1
    assert stats.lines == 2  # the two comment lines and the blank line do not count


# --- merging ---------------------------------------------------------------------


def test_lines_with_the_same_simplified_form_and_pinyin_merge_senses_in_file_order() -> None:
    lines = [
        "行 行 [xing2] /to walk; to go/",
        "行 行 [xing2] /capable; competent/",
    ]
    entries = list(convert_lines(lines))
    assert len(entries) == 1
    assert [s.definition for s in entries[0].senses] == ["to walk; to go", "capable; competent"]


def test_lines_with_the_same_simplified_but_different_readings_merge_too() -> None:
    lines = [
        "行 行 [hang2] /row; line/",
        "行 行 [heng2] /used in 道行[dao4 heng2]/",
        "行 行 [xing2] /to walk; to go/",
    ]
    entries = list(convert_lines(lines))
    assert len(entries) == 1
    entry = entries[0]
    assert [s.definition for s in entry.senses] == [
        "row; line",
        'used in <a href="lex:道行">道行</a>',
        "to walk; to go",
    ]
    assert {f.form for f in entry.forms if f.tag == "pinyin"} == {
        "háng",
        "héng",
        "xíng",
    }


def test_non_adjacent_lines_for_the_same_simplified_form_still_merge() -> None:
    # CC-CEDICT sorts by Traditional, so `裡` and `里` (both simplify to
    # `里`) are far apart in the real file; the converter must not rely on
    # adjacency.
    lines = [
        "裡 里 [li3] /lining; interior/",
        "中国 中国 [Zhong1 guo2] /China/",
        "里 里 [li3] /neighborhood/",
    ]
    entries = list(convert_lines(lines))
    assert [e.headword for e in entries] == ["里", "中国"]
    li_entry = entries[0]
    assert [s.definition for s in li_entry.senses] == ["lining; interior", "neighborhood"]
    assert [(f.form, f.tag) for f in li_entry.forms] == [("裡", "traditional"), ("lǐ", "pinyin")]


# --- forms -------------------------------------------------------------------------


def test_traditional_form_is_added_only_when_it_differs() -> None:
    lines = [
        "電腦 电脑 [dian4 nao3] /computer/",  # traditional differs
        "中 中 [zhong1] /middle/",  # traditional == simplified
    ]
    entries = list(convert_lines(lines))
    computer, zhong = entries
    assert [(f.form, f.tag) for f in computer.forms if f.tag == "traditional"] == [
        ("電腦", "traditional")
    ]
    assert [f for f in zhong.forms if f.tag == "traditional"] == []


def test_pinyin_forms_include_spaced_and_concatenated_spellings() -> None:
    lines = ["你好 你好 [ni3 hao3] /hello; hi/"]
    entries = list(convert_lines(lines))
    pinyin_forms_ = {f.form for f in entries[0].forms if f.tag == "pinyin"}
    assert pinyin_forms_ == {"nǐ hǎo", "nǐhǎo"}


def test_single_syllable_pinyin_is_not_duplicated_as_a_form() -> None:
    lines = ["中 中 [zhong1] /middle/"]
    entries = list(convert_lines(lines))
    pinyin_forms_ = [f.form for f in entries[0].forms if f.tag == "pinyin"]
    assert pinyin_forms_ == ["zhōng"]


# --- classifiers ----------------------------------------------------------------


def test_classifier_gloss_becomes_the_label_of_the_preceding_definition() -> None:
    lines = [
        "上衣 上衣 [shang4 yi1] /jacket/upper outer garment/CL:件[jian4]/",
    ]
    entries = list(convert_lines(lines))
    senses = entries[0].senses
    assert [s.definition for s in senses] == ["jacket", "upper outer garment"]
    assert senses[0].label is None
    assert senses[1].label == "CL: 件 (jiàn)"


def test_multiple_classifiers_on_one_line_attach_to_their_own_preceding_sense() -> None:
    lines = [
        "棋 棋 [qi2] /chess/chess-like game/a game of chess/CL:盤|盘[pan2]/"
        "chess piece/CL:個|个[ge4],顆|颗[ke1]/",
    ]
    entries = list(convert_lines(lines))
    senses = entries[0].senses
    assert [s.definition for s in senses] == [
        "chess",
        "chess-like game",
        "a game of chess",
        "chess piece",
    ]
    assert [s.label for s in senses] == [
        None,
        None,
        "CL: 盤/盘 (pán)",
        "CL: 個/个 (gè); 顆/颗 (kē)",
    ]


def test_classifier_with_no_preceding_sense_on_the_line_is_dropped() -> None:
    # Defensive: CC-CEDICT never emits this shape, but the parser must not crash.
    lines = ["一事 一事 [yi1 shi4] /CL:件[jian4]/incident/"]
    entries = list(convert_lines(lines))
    assert [s.definition for s in entries[0].senses] == ["incident"]
    assert entries[0].senses[0].label is None


# --- markup validation -----------------------------------------------------------


def test_every_emitted_definition_passes_the_html_subset_validator() -> None:
    lines = [
        "你好 你好 [ni3 hao3] /hello; hi/see 嗨[hai1]/",
        "上衣 上衣 [shang4 yi1] /jacket/CL:件[jian4]/",
    ]
    for entry in convert_lines(lines):
        for sense in entry.senses:
            assert validate(sense.definition) == (), sense.definition


# --- surname entries ---------------------------------------------------------------


def test_surname_glosses_are_kept_as_plain_senses() -> None:
    lines = ["丁 丁 [Ding1] /surname Ding/"]
    entries = list(convert_lines(lines))
    assert entries[0].senses[0].definition == "surname Ding"


# --- headword normalization round-trip -------------------------------------------


def test_toned_pinyin_forms_normalize_to_match_a_plain_query() -> None:
    # The stored form is "nǐ hǎo" (spaced) and "nǐhǎo" (concatenated);
    # normalize_headword strips diacritics but never removes spaces, so a query
    # needs the matching spelling (with or without the space) to normalize equal.
    spaced, concatenated = pinyin_forms("ni3 hao3")
    assert normalize_headword("ni hao") == normalize_headword(spaced) == "ni hao"
    assert normalize_headword("nihao") == normalize_headword(concatenated) == "nihao"
    assert normalize_headword("nǐhǎo") == normalize_headword(concatenated)


# --- header date / bundle version -------------------------------------------------


def test_bundle_version_uses_the_header_date_month() -> None:
    assert bundle_version(date(2026, 9, 27)) == "2026.09.1"
    assert bundle_version(date(2026, 10, 1), 3) == "2026.10.3"


def _write_dump(path: Path, header_date: str, lines: list[str]) -> None:
    header = [
        "# CC-CEDICT",
        "# Community maintained free Chinese-English dictionary.",
        "#",
        "# License:",
        "# Creative Commons Attribution-ShareAlike 4.0 International License",
        "# https://creativecommons.org/licenses/by-sa/4.0/",
        "#! version=1",
        "#! subversion=0",
        f"#! entries={len(lines)}",
        "#! publisher=MDBG",
        "#! license=https://creativecommons.org/licenses/by-sa/4.0/",
        f"#! date={header_date}",
    ]
    with gzip.open(path, "wt", encoding="utf-8") as handle:
        for line in header + lines:
            handle.write(line + "\n")


_SAMPLE_LINES = [
    "你好 你好 [ni3 hao3] /hello; hi/",
    "中國 中国 [Zhong1 guo2] /China/",
    "學習 学习 [xue2 xi2] /to learn; to study/",
    "電腦 电脑 [dian4 nao3] /computer/CL:臺|台[tai2]/",
    "謝謝 谢谢 [xie4 xie5] /to thank; thanks/",
]


def test_header_date_is_read_from_the_hash_bang_line(tmp_path: Path) -> None:
    dump = tmp_path / "cedict.txt.gz"
    _write_dump(dump, "2026-09-27T12:50:23Z", _SAMPLE_LINES)
    assert read_header_date(dump) == date(2026, 9, 27)


def test_converter_contract_over_a_small_dump(tmp_path: Path) -> None:
    cache = tmp_path / "cache"
    cache.mkdir()
    _write_dump(cache / "cedict_1_0_ts_utf-8_mdbg.txt.gz", "2026-09-27T12:50:23Z", _SAMPLE_LINES)
    converter = CedictConverter(cache)
    specs = converter.dictionaries()
    assert [(s.dict_id, s.version, s.kind, s.source_lang, s.target_lang) for s in specs] == [
        ("cedict-zh-en", "2026.09.1", "bilingual", "zh", "en")
    ]
    entries = list(converter.iter_records(specs[0]))
    assert {e.headword for e in entries} == {
        "你好",
        "中国",
        "学习",
        "电脑",
        "谢谢",
    }
    assert converter.stats.entries == 5


def test_build_a_tiny_bundle_from_the_sample_lines(tmp_path: Path) -> None:
    entries: list[Entry] = list(convert_lines(_SAMPLE_LINES))
    spec = spec_for("2026.09.1")
    out_dir = tmp_path / "out"
    build_bundle(entries, spec, out_dir, vacuum=False)
    conn = sqlite3.connect(out_dir / "dict.sqlite")
    try:
        meta = dict(conn.execute("SELECT key, value FROM meta").fetchall())
        assert meta["dict_id"] == "cedict-zh-en"
        assert meta["entry_count"] == "5"
        assert "CC-CEDICT" in meta["attribution"]
        assert meta["license"] == "CC-BY-SA-4.0"

        # "nihao" / "ni hao" / toned "nǐhǎo" all find 你好 through forms.
        for query in ("nihao", "ni hao", "nǐhǎo"):
            norm = normalize_headword(query)
            rows = conn.execute(
                "SELECT e.headword FROM forms f JOIN entries e ON e.id = f.entry_id "
                "WHERE f.form_norm = ?",
                (norm,),
            ).fetchall()
            assert rows == [("你好",)], f"{query!r} (normalized {norm!r}) did not match"

        # The traditional form (電腦) finds the simplified headword (电脑).
        rows = conn.execute(
            "SELECT e.headword FROM forms f JOIN entries e ON e.id = f.entry_id "
            "WHERE f.form_norm = ?",
            (normalize_headword("電腦"),),
        ).fetchall()
        assert rows == [("电脑",)]
    finally:
        conn.close()


def test_cli_rejects_an_unknown_dictionary() -> None:
    assert cedict.main(["--dict", "cedict-ja-en", "--no-fetch"]) == 1
