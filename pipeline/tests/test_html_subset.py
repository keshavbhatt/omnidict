"""Tests for `omnipipe.html_subset`."""

from __future__ import annotations

import pytest

from omnipipe.html_subset import is_valid, sanitize, to_plain, validate


@pytest.mark.parametrize(
    "text",
    [
        "<b>perhaps</b>",
        "plain text",
        "a &amp; b",
        '<span class="pos">adv</span>',
        '<a href="lex:word">word</a>',
        "<i>x</i><sub>2</sub><sup>3</sup><u>y</u>",
        "line one<br>line two",
    ],
)
def test_canonical_input_round_trips_with_zero_warnings(text: str) -> None:
    result = sanitize(text)
    assert result.html == text
    assert result.warnings == ()
    assert is_valid(text)


def test_script_content_is_dropped() -> None:
    result = sanitize("before<script>alert(1)</script>after")
    assert result.html == "beforeafter"
    assert len(result.warnings) == 1


def test_style_content_is_dropped() -> None:
    result = sanitize("<style>.x{color:red}</style>text")
    assert result.html == "text"
    assert len(result.warnings) == 1


def test_disallowed_tag_is_unwrapped_keeping_text() -> None:
    result = sanitize("<div>hello</div>")
    assert result.html == "hello"
    assert len(result.warnings) == 1


def test_bad_href_scheme_is_unwrapped() -> None:
    result = sanitize('<a href="javascript:alert(1)">bad</a>')
    assert result.html == "bad"
    assert len(result.warnings) == 1


def test_missing_href_is_unwrapped() -> None:
    result = sanitize("<a>bad</a>")
    assert result.html == "bad"
    assert len(result.warnings) == 1


def test_empty_href_target_is_unwrapped() -> None:
    result = sanitize('<a href="lex:">bad</a>')
    assert result.html == "bad"
    assert len(result.warnings) == 1


def test_extra_attribute_is_dropped() -> None:
    result = sanitize('<b class="x">bold</b>')
    assert result.html == "<b>bold</b>"
    assert len(result.warnings) == 1


def test_extra_attribute_on_span_is_dropped_but_span_kept() -> None:
    result = sanitize('<span class="pos" id="x">adv</span>')
    assert result.html == '<span class="pos">adv</span>'
    assert len(result.warnings) == 1


def test_invalid_span_class_is_unwrapped() -> None:
    result = sanitize('<span class="bogus">adv</span>')
    assert result.html == "adv"
    assert len(result.warnings) == 1


def test_missing_span_class_is_unwrapped() -> None:
    result = sanitize("<span>adv</span>")
    assert result.html == "adv"
    assert len(result.warnings) == 1


def test_unclosed_tag_is_closed_at_end() -> None:
    result = sanitize("<b>bold")
    assert result.html == "<b>bold</b>"
    assert len(result.warnings) == 1


def test_stray_end_tag_is_dropped() -> None:
    result = sanitize("</b>text")
    assert result.html == "text"
    assert len(result.warnings) == 1


def test_comment_is_dropped() -> None:
    result = sanitize("<!-- hidden -->text")
    assert result.html == "text"
    assert len(result.warnings) == 1


def test_doctype_is_dropped() -> None:
    result = sanitize("<!DOCTYPE html>text")
    assert result.html == "text"
    assert len(result.warnings) == 1


def test_escaping_of_angle_brackets_and_ampersand() -> None:
    result = sanitize("1 < 2 & 3 > 0")
    assert result.html == "1 &lt; 2 &amp; 3 &gt; 0"
    assert result.warnings == ()


def test_br_is_void_and_canonical() -> None:
    assert sanitize("<br>").html == "<br>"
    assert sanitize("<br/>").html == "<br>"
    assert sanitize('<br class="x">').warnings != ()


def test_validate_matches_sanitize_warnings() -> None:
    text = "<div>hi</div>"
    assert validate(text) == sanitize(text).warnings


def test_to_plain_strips_tags_and_decodes_entities() -> None:
    assert to_plain("<b>perhaps</b> &amp; <i>maybe</i>") == "perhaps & maybe"


def test_to_plain_br_becomes_space() -> None:
    assert to_plain("line one<br>line two") == "line one line two"


def test_to_plain_collapses_whitespace_and_trims() -> None:
    assert to_plain("  a   b\nc  ") == "a b c"
