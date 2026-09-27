"""Tests for `omnipipe.render` (DOCS/entry-html.md, the entry HTML contract)."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from omnipipe.render import render_entry
from omnipipe.schema import Entry

# Resolved the same way `tests/conftest.py`'s `repo_root` fixture resolves it,
# but at collection time so it can drive `parametrize`.
_REPO_ROOT = Path(__file__).resolve().parents[2]
_GOLDEN_DIR = _REPO_ROOT / "tests" / "golden" / "entries"
_GOLDEN_JSON_PATHS = sorted(_GOLDEN_DIR.glob("*.json"))


def _entry_from(json_path: Path) -> Entry:
    return Entry.from_json(json.loads(json_path.read_text(encoding="utf-8")))


# -- golden files ------------------------------------------------------------


def test_golden_directory_is_not_empty() -> None:
    assert _GOLDEN_JSON_PATHS, f"no golden entries found under {_GOLDEN_DIR}"


@pytest.mark.parametrize("json_path", _GOLDEN_JSON_PATHS, ids=lambda p: p.stem)
def test_render_entry_matches_golden_html(json_path: Path) -> None:
    entry = _entry_from(json_path)
    expected = json_path.with_suffix(".html").read_text(encoding="utf-8")
    assert render_entry(entry) == expected


def test_every_golden_json_has_a_matching_html_and_vice_versa() -> None:
    json_stems = {p.stem for p in _GOLDEN_DIR.glob("*.json")}
    html_stems = {p.stem for p in _GOLDEN_DIR.glob("*.html")}
    assert json_stems == html_stems


# -- escaping -----------------------------------------------------------------


def test_escaping_replaces_amp_lt_gt_quote_but_leaves_apostrophe_untouched() -> None:
    raw = """A & B <C> "D" 'E'"""
    entry = Entry.from_json({"headword": raw, "lang": "en", "senses": [{"definition": "x"}]})
    html = render_entry(entry)
    assert "<span class=\"hw\">A &amp; B &lt;C&gt; &quot;D&quot; 'E'</span>" in html
    assert "'" in html
    assert "&#39;" not in html
    assert "&apos;" not in html


def test_escaping_applies_to_relation_targets_and_forms() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [{"definition": "x"}],
            "forms": [{"form": "a&b", "tag": "<t>"}],
            "relations": [{"type": "synonym", "target": '"y"'}],
        }
    )
    html = render_entry(entry)
    assert '<span class="form">a&amp;b</span>' in html
    assert '<span class="form-tag">(&lt;t&gt;)</span>' in html
    assert '<a href="lex:&quot;y&quot;">&quot;y&quot;</a>' in html


def test_definition_and_example_html_is_inserted_verbatim_not_escaped() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [
                {
                    "definition": '<b>bold</b> & <a href="lex:y">y</a>',
                    "examples": [{"text": "line<br>two", "translation": "t<i>r</i>"}],
                }
            ],
        }
    )
    html = render_entry(entry)
    assert '<p class="def"><b>bold</b> & <a href="lex:y">y</a></p>' in html
    assert '<span class="ex">line<br>two</span>' in html
    assert '<span class="tr">t<i>r</i></span>' in html


# -- frequency stars -----------------------------------------------------------


@pytest.mark.parametrize(
    ("frequency", "stars"),
    [
        (1, "◆◇◇◇◇"),
        (3, "◆◆◆◇◇"),
        (5, "◆◆◆◆◆"),
    ],
)
def test_stars_are_filled_diamonds_then_empty_diamonds(frequency: int, stars: str) -> None:
    entry = Entry.from_json(
        {"headword": "x", "lang": "en", "frequency": frequency, "senses": [{"definition": "x"}]}
    )
    html = render_entry(entry)
    assert f'<span class="freq">{stars}</span>' in html


def test_freq_span_omitted_when_frequency_absent() -> None:
    entry = Entry.from_json({"headword": "x", "lang": "en", "senses": [{"definition": "x"}]})
    assert "freq" not in render_entry(entry)


# -- omission rules -------------------------------------------------------------


def test_empty_pattern_label_translation_are_treated_as_absent() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [
                {
                    "pattern": "",
                    "label": "",
                    "definition": "d",
                    "examples": [{"text": "e", "translation": ""}],
                }
            ],
        }
    )
    html = render_entry(entry)
    assert "pattern" not in html
    assert "label" not in html
    assert '<span class="tr">' not in html


def test_pos_other_has_no_pos_span() -> None:
    entry = Entry.from_json(
        {"headword": "x", "lang": "en", "senses": [{"pos": "other", "definition": "d"}]}
    )
    assert '<span class="pos">' not in render_entry(entry)


def test_pos_absent_has_no_pos_span() -> None:
    entry = Entry.from_json({"headword": "x", "lang": "en", "senses": [{"definition": "d"}]})
    assert '<span class="pos">' not in render_entry(entry)


def test_pronunciation_without_ipa_is_skipped_and_paragraph_omitted() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "pronunciations": [{"region": "US"}],
            "senses": [{"definition": "d"}],
        }
    )
    assert '<p class="prons">' not in render_entry(entry)


def test_prons_paragraph_present_when_at_least_one_has_ipa() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "pronunciations": [{"region": "US"}, {"ipa": "p"}],
            "senses": [{"definition": "d"}],
        }
    )
    html = render_entry(entry)
    assert '<p class="prons"><span class="ipa">p</span></p>' in html


def test_examples_ul_omitted_when_no_examples() -> None:
    entry = Entry.from_json({"headword": "x", "lang": "en", "senses": [{"definition": "d"}]})
    assert "<ul" not in render_entry(entry)


def test_forms_paragraph_omitted_when_no_forms() -> None:
    entry = Entry.from_json({"headword": "x", "lang": "en", "senses": [{"definition": "d"}]})
    assert '<p class="forms">' not in render_entry(entry)


def test_forms_paragraph_omitted_when_only_romanization_forms_present() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [{"definition": "d"}],
            "forms": [{"form": "roman", "tag": "romanization"}],
        }
    )
    html = render_entry(entry)
    assert '<span class="roman">roman</span>' in html
    assert '<p class="forms">' not in html


# -- forms: 12-form cap and romanization handling -------------------------------


def test_forms_cap_at_twelve_excludes_romanization_and_preserves_order() -> None:
    forms = [{"form": f"f{i:02d}", "tag": "variant"} for i in range(1, 16)]
    forms.insert(1, {"form": "rom-a", "tag": "romanization"})
    forms.insert(7, {"form": "rom-b", "tag": "romanization"})
    entry = Entry.from_json(
        {"headword": "x", "lang": "en", "senses": [{"definition": "d"}], "forms": forms}
    )
    html = render_entry(entry)
    shown = [f"f{i:02d}" for i in range(1, 13)]
    for form in shown:
        assert f'<span class="form">{form}</span>' in html
    for form in ["f13", "f14", "f15"]:
        assert f'<span class="form">{form}</span>' not in html
    assert '<span class="roman">rom-a</span>' in html
    assert "rom-b" not in html


def test_first_romanization_used_regardless_of_position() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [{"definition": "d"}],
            "forms": [
                {"form": "plain", "tag": "variant"},
                {"form": "first-roman", "tag": "romanization"},
                {"form": "second-roman", "tag": "romanization"},
            ],
        }
    )
    html = render_entry(entry)
    assert '<span class="roman">first-roman</span>' in html
    assert "second-roman" not in html


# -- relation grouping and order ------------------------------------------------


def test_relations_are_grouped_and_ordered_synonym_antonym_see_derived() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [{"definition": "d"}],
            "relations": [
                {"type": "derived", "target": "d1"},
                {"type": "see", "target": "s1"},
                {"type": "antonym", "target": "a1"},
                {"type": "synonym", "target": "syn1"},
            ],
        }
    )
    html = render_entry(entry)
    syn_pos = html.index("Synonyms")
    ant_pos = html.index("Antonyms")
    see_pos = html.index("See also")
    der_pos = html.index("Derived terms")
    assert syn_pos < ant_pos < see_pos < der_pos


def test_several_relations_of_one_type_are_comma_joined_in_stored_order() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [{"definition": "d"}],
            "relations": [
                {"type": "synonym", "target": "b"},
                {"type": "synonym", "target": "a"},
            ],
        }
    )
    html = render_entry(entry)
    assert '<a href="lex:b">b</a>, <a href="lex:a">a</a>' in html


def test_sense_relation_renders_under_its_sense_not_entry_level() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [{"definition": "one"}, {"definition": "two"}],
            "relations": [{"type": "synonym", "target": "syn", "sense_ordinal": 2}],
        }
    )
    html = render_entry(entry)
    sense_divs = html.split('<div class="sense">')
    assert "Synonyms" not in sense_divs[1]
    assert "Synonyms" in sense_divs[2]


def test_relation_with_sense_ordinal_matching_no_sense_renders_at_entry_level() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [{"definition": "one"}],
            "relations": [{"type": "synonym", "target": "syn", "sense_ordinal": 7}],
        }
    )
    html = render_entry(entry)
    before_sense_closes, after_sense_closes = html.split("</div>", 1)
    assert "Synonyms" not in before_sense_closes
    assert "Synonyms" in after_sense_closes
    assert 'href="lex:syn"' in html


def test_relation_without_sense_ordinal_renders_at_entry_level() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [{"definition": "one"}],
            "relations": [{"type": "derived", "target": "der"}],
        }
    )
    html = render_entry(entry)
    lines = html.splitlines()
    sense_close_idx = lines.index("</div>")
    rel_idx = next(i for i, line in enumerate(lines) if "Derived terms" in line)
    assert rel_idx > sense_close_idx


def test_relation_type_with_no_relations_produces_no_paragraph() -> None:
    entry = Entry.from_json(
        {
            "headword": "x",
            "lang": "en",
            "senses": [{"definition": "d"}],
            "relations": [{"type": "synonym", "target": "syn"}],
        }
    )
    html = render_entry(entry)
    assert "Antonyms" not in html
    assert "See also" not in html
    assert "Derived terms" not in html


# -- output framing --------------------------------------------------------------


def test_output_ends_with_single_trailing_newline() -> None:
    entry = Entry.from_json({"headword": "x", "lang": "en", "senses": [{"definition": "d"}]})
    html = render_entry(entry)
    assert html.endswith("\n")
    assert not html.endswith("\n\n")


def test_top_level_structure_is_one_entry_div() -> None:
    entry = Entry.from_json({"headword": "x", "lang": "en", "senses": [{"definition": "d"}]})
    html = render_entry(entry)
    lines = html.split("\n")
    assert lines[0] == '<div class="entry" lang="en">'
    assert lines[-2] == "</div>"
    assert lines[-1] == ""
