"""Tests for `omnipipe.converters.kengdic`.

The sample rows are hand-written in Kengdic's shape; no dictionary text is copied.
"""

from __future__ import annotations

import dataclasses
import io
import json
import sqlite3
import urllib.error
import urllib.request
from collections.abc import Callable
from datetime import date
from email.message import Message
from pathlib import Path

import pytest

from omnipipe.build import build_bundle
from omnipipe.converters import kengdic
from omnipipe.converters.kengdic import (
    ConversionStats,
    KengdicConverter,
    KengdicError,
    bundle_version,
    convert_rows,
    fetch_dump,
    parse_row,
    read_dump_info,
    spec_for,
)
from omnipipe.html_subset import validate
from omnipipe.normalize import normalize_headword
from omnipipe.schema import Entry

HEADER = "id\tsurface\thanja\tgloss\tlevel\tcreated\tsource\n"


def row(surface: str, gloss: str = "", hanja: str = "", level: str = "") -> dict[str, str]:
    return {"surface": surface, "hanja": hanja, "gloss": gloss, "level": level}


def convert(*rows: dict[str, str]) -> list[Entry]:
    return list(convert_rows(rows))


def tsv(*lines: str) -> str:
    return HEADER + "".join(
        f"{i}\t{line}\t2009-01-01T00:00:00Z\tx\n" for i, line in enumerate(lines)
    )


# --- rows ---------------------------------------------------------------------


def test_parse_row_collapses_whitespace_and_splits_hanja() -> None:
    parsed = parse_row(row("  가나  다 ", " a  b ", "佳那, 家羅 ", "B"))
    assert parsed is not None
    assert parsed.surface == "가나 다"
    assert parsed.gloss == "a b"
    assert parsed.hanja == ("佳那", "家羅")
    assert parsed.level == "B"


def test_parse_row_without_a_surface_is_none() -> None:
    assert parse_row(row("   ", "gloss")) is None


# --- grouping -------------------------------------------------------------------


def test_rows_for_the_same_word_merge_into_one_entry_in_row_order() -> None:
    entries = convert(row("가다", "to go"), row("오다", "to come"), row("가다", "to leave"))
    assert [e.headword for e in entries] == ["가다", "오다"]
    assert [s.definition for s in entries[0].senses] == ["to go", "to leave"]


def test_spacing_differences_in_the_surface_do_not_split_an_entry() -> None:
    entries = convert(row("사과 하다", "to apologize"), row("사과  하다", "to say sorry"))
    assert len(entries) == 1
    assert len(entries[0].senses) == 2


def test_a_gloss_repeated_ignoring_case_is_kept_once() -> None:
    stats = ConversionStats()
    entries = list(convert_rows([row("물", "Water"), row("물", "water")], stats))
    assert [s.definition for s in entries[0].senses] == ["Water"]
    assert stats.repeated == 1


def test_rows_without_a_gloss_add_only_their_hanja() -> None:
    stats = ConversionStats()
    entries = list(convert_rows([row("초월", hanja="超越"), row("초월", "transcendence")], stats))
    assert [s.definition for s in entries[0].senses] == ["transcendence"]
    assert [(f.form, f.tag) for f in entries[0].forms] == [("超越", "hanja")]
    assert stats.no_gloss == 1


def test_a_word_with_no_gloss_at_all_is_dropped() -> None:
    stats = ConversionStats()
    assert list(convert_rows([row("호연", hanja="浩然")], stats)) == []
    assert stats.entries == 0


def test_hanja_forms_are_not_repeated_and_skip_the_headword_itself() -> None:
    entries = convert(
        row("포식하다", "to feast", "捕食하다,飽食하다"),
        row("포식하다", "to prey on", "捕食하다"),
        row("ABC", "letters", "ABC"),
    )
    assert [f.form for f in entries[0].forms] == ["捕食하다", "飽食하다"]
    assert entries[1].forms == ()


def test_levels_become_frequency_bands_keeping_the_highest() -> None:
    entries = convert(
        row("가", "a", level="C"),
        row("가", "b", level="A"),
        row("나", "c", level="D"),
        row("다", "d", level="B"),
    )
    assert [e.frequency for e in entries] == [5, None, 4]


def test_graded_senses_come_first_the_rest_keep_row_order() -> None:
    [entry] = convert(
        row("물", "a dye"),
        row("물", "a tide"),
        row("물", "water", level="A"),
        row("물", "a liquid", level="C"),
    )
    assert [s.definition for s in entry.senses] == ["water", "a liquid", "a dye", "a tide"]


def test_every_definition_is_escaped_and_passes_the_html_subset_validator() -> None:
    entries = convert(row("비교", "a < b & c > d"), row("태그", "<b>bold</b>"))
    for entry in entries:
        for sense in entry.senses:
            validate(sense.definition)
    assert entries[0].senses[0].definition == "a &lt; b &amp; c &gt; d"


def test_hanja_forms_normalize_to_match_a_hanja_query() -> None:
    [entry] = convert(row("초월", "transcendence", "超越"))
    assert normalize_headword("超越") in {normalize_headword(f.form) for f in entry.forms}


# --- files and fetch -------------------------------------------------------------


def test_read_rows_rejects_an_unexpected_header(tmp_path: Path) -> None:
    dump = tmp_path / "kengdic.tsv"
    dump.write_text("id\tword\n1\tx\n", encoding="utf-8")
    with pytest.raises(KengdicError, match="unexpected header"):
        list(kengdic.read_rows(dump))


def test_bundle_version_uses_the_commit_month() -> None:
    assert bundle_version(date(2022, 7, 22)) == "2022.07.1"
    assert bundle_version(date(2022, 7, 22), 3) == "2022.07.3"


class FakeResponse(io.BytesIO):
    def __enter__(self) -> FakeResponse:
        return self


def _fake_github(
    requests: list[str], commit: str = "abc123"
) -> Callable[[urllib.request.Request, float], FakeResponse]:
    def urlopen(request: urllib.request.Request, timeout: float) -> FakeResponse:
        requests.append(request.full_url)
        if request.full_url == kengdic.COMMITS_URL:
            reply = [{"sha": commit, "commit": {"committer": {"date": "2022-07-22T09:48:06Z"}}}]
            return FakeResponse(json.dumps(reply).encode())
        assert request.full_url == kengdic.raw_url(commit)
        return FakeResponse(tsv("가\t\tgo\t").encode())

    return urlopen


def test_fetch_pins_the_last_commit_and_skips_a_current_copy(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    requests: list[str] = []
    monkeypatch.setattr(urllib.request, "urlopen", _fake_github(requests))
    dump = fetch_dump(tmp_path)
    info = read_dump_info(dump)
    assert info.commit == "abc123"
    assert info.commit_date == date(2022, 7, 22)
    assert info.url == kengdic.raw_url("abc123")
    assert len(requests) == 2

    fetch_dump(tmp_path)
    assert requests[2:] == [kengdic.COMMITS_URL]


def test_fetch_failure_is_a_kengdic_error_and_leaves_no_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def failing(request: urllib.request.Request, timeout: float) -> FakeResponse:
        if request.full_url == kengdic.COMMITS_URL:
            return _fake_github([])(request, timeout)
        raise urllib.error.HTTPError(request.full_url, 404, "Not Found", Message(), None)

    monkeypatch.setattr(urllib.request, "urlopen", failing)
    with pytest.raises(KengdicError, match="HTTP 404"):
        fetch_dump(tmp_path)
    assert list(tmp_path.iterdir()) == []


def test_an_unexpected_commits_reply_is_a_kengdic_error(monkeypatch: pytest.MonkeyPatch) -> None:
    def empty(request: urllib.request.Request, timeout: float) -> FakeResponse:
        return FakeResponse(b"[]")

    monkeypatch.setattr(urllib.request, "urlopen", empty)
    with pytest.raises(KengdicError, match="unexpected reply"):
        kengdic.latest_commit()


# --- converter contract and a real build ------------------------------------------


def _cache(tmp_path: Path) -> Path:
    cache = tmp_path / "cache"
    cache.mkdir()
    (cache / "kengdic.tsv").write_text(
        tsv(
            "사과\t\tan apology\t",
            "사과\t沙果\tan apple\tA",
            "가다\t\tto go\tA",
            "초월\t超越\ttranscendence, overcoming\t",
            "호연\t浩然\t\t",
        ),
        encoding="utf-8",
    )
    info = kengdic.DumpInfo(
        kengdic.raw_url("abc"), "abc", date(2022, 7, 22), "2026-09-28T00:00:00Z"
    )
    (cache / "kengdic.tsv.json").write_text(json.dumps(info.to_json()), encoding="utf-8")
    return cache


def test_converter_contract_over_a_small_file(tmp_path: Path) -> None:
    converter = KengdicConverter(_cache(tmp_path))
    [spec] = converter.dictionaries()
    assert spec.dict_id == "kengdic-ko-en"
    assert spec.version == "2022.07.1"
    assert spec.license == "MPL-2.0 OR LGPL-2.0-or-later"
    assert spec.source_url == "https://github.com/garfieldnate/kengdic/blob/abc/kengdic.tsv"
    assert (spec.source_lang_name, spec.target_lang_name) == ("Korean", "English")
    entries = list(converter.iter_records(spec))
    assert [e.headword for e in entries] == ["사과", "가다", "초월"]
    assert converter.stats.no_gloss == 1


def test_converter_rejects_another_dictionary(tmp_path: Path) -> None:
    converter = KengdicConverter(_cache(tmp_path))
    other = spec_for("2022.07.1")
    with pytest.raises(KengdicError, match="unknown dictionary"):
        list(converter.iter_records(dataclasses.replace(other, dict_id="x")))


def test_build_a_tiny_bundle_from_the_sample_rows(tmp_path: Path) -> None:
    converter = KengdicConverter(_cache(tmp_path))
    [spec] = converter.dictionaries()
    out = build_bundle(converter.iter_records(spec), spec, tmp_path / "out")
    with sqlite3.connect(out) as db:
        headwords = [r[0] for r in db.execute("SELECT headword FROM entries ORDER BY id")]
        assert headwords == ["사과", "가다", "초월"]
        [(entry_id,)] = db.execute("SELECT entry_id FROM forms WHERE form = '超越'").fetchall()
        assert db.execute("SELECT headword FROM entries WHERE id = ?", (entry_id,)).fetchone() == (
            "초월",
        )


def test_cli_rejects_an_unknown_dictionary() -> None:
    assert kengdic.main(["--dict", "kengdic-xx"]) == 1
