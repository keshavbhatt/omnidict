"""Tests for `omnipipe.build`."""

from __future__ import annotations

import filecmp
import json
import re
import sqlite3
from datetime import UTC, datetime
from pathlib import Path

import pytest

from omnipipe.build import BuildError, build_bundle, read_jsonl
from omnipipe.schema import DictSpec, Entry, SchemaError

_BUILT_AT = datetime(2026, 9, 27, tzinfo=UTC)

_EXPECTED_TABLES = {
    "meta",
    "entries",
    "pronunciations",
    "senses",
    "examples",
    "forms",
    "relations",
    "fts",
    "fts_map",
    "fts_data",
    "fts_idx",
    "fts_docsize",
    "fts_config",
}


def _load_spec(fixtures_dir: Path) -> DictSpec:
    meta_obj = json.loads((fixtures_dir / "sample-en.meta.json").read_text(encoding="utf-8"))
    return DictSpec.from_json(meta_obj)


def _build(fixtures_dir: Path, out_dir: Path) -> Path:
    spec = _load_spec(fixtures_dir)
    entries = list(read_jsonl(fixtures_dir / "sample-en.jsonl"))
    return build_bundle(entries, spec, out_dir, built_at=_BUILT_AT)


def test_build_sample_fixture(fixtures_dir: Path, tmp_path: Path) -> None:
    db_path = _build(fixtures_dir, tmp_path)
    assert db_path == tmp_path / "dict.sqlite"

    conn = sqlite3.connect(db_path)
    try:
        (integrity,) = conn.execute("PRAGMA integrity_check").fetchone()
        assert integrity == "ok"

        meta = dict(conn.execute("SELECT key, value FROM meta").fetchall())
        required_keys = {
            "dict_id",
            "name",
            "source_lang",
            "target_lang",
            "version",
            "schema_version",
            "publisher",
            "license",
            "license_url",
            "attribution",
            "entry_count",
            "built_at",
            "kind",
            "icu_version",
            "unicode_version",
        }
        assert required_keys <= meta.keys()
        assert meta["entry_count"] == "14"
        assert meta["schema_version"] == "2"

        tables = {
            row[0]
            for row in conn.execute("SELECT name FROM sqlite_master WHERE type IN ('table','view')")
        }
        assert tables >= _EXPECTED_TABLES

        (cafe_norm,) = conn.execute(
            "SELECT headword_norm FROM entries WHERE headword = ?", ("café",)
        ).fetchone()
        assert cafe_norm == "cafe"

        run_forms = {
            row[0]
            for row in conn.execute(
                "SELECT form FROM forms JOIN entries ON entries.id = forms.entry_id "
                "WHERE entries.headword = ?",
                ("run",),
            )
        }
        assert "ran" in run_forms

        previews = [row[0] for row in conn.execute("SELECT preview FROM entries")]
        assert all(previews)
        assert all(len(p) <= 160 for p in previews)

        rows = conn.execute(
            "SELECT entries.headword FROM fts "
            "JOIN fts_map ON fts_map.rowid = fts.rowid "
            "JOIN entries ON entries.id = fts_map.entry_id "
            "WHERE fts MATCH 'fortunate'"
        ).fetchall()
        assert [r[0] for r in rows] == ["serendipity"]

        ordered = [row[0] for row in conn.execute("SELECT headword FROM entries ORDER BY sort_key")]
        assert ordered.index("book") < ordered.index("café") < ordered.index("dictionary")
        lowered = [h.lower() for h in ordered]
        assert lowered.index("bookmark") < lowered.index("monday") < lowered.index("mouse")
    finally:
        conn.close()


def test_build_is_deterministic(fixtures_dir: Path, tmp_path: Path) -> None:
    out1 = tmp_path / "a"
    out2 = tmp_path / "b"
    db1 = _build(fixtures_dir, out1)
    db2 = _build(fixtures_dir, out2)
    assert filecmp.cmp(db1, db2, shallow=False)


def test_script_tag_in_definition_raises(tmp_path: Path) -> None:
    spec = DictSpec.from_json(
        {
            "dict_id": "sample-en",
            "name": "Sample English",
            "source_lang": "en",
            "target_lang": "en",
            "version": "1",
            "publisher": "p",
            "license": "l",
            "license_url": "u",
            "attribution": "a",
            "kind": "monolingual",
        }
    )
    entry = Entry.from_json(
        {
            "headword": "bad",
            "lang": "en",
            "senses": [{"definition": "<script>alert(1)</script>evil"}],
        }
    )
    with pytest.raises(BuildError):
        build_bundle([entry], spec, tmp_path)


def test_whitespace_only_definition_raises() -> None:
    with pytest.raises(SchemaError):
        Entry.from_json({"headword": "bad", "lang": "en", "senses": [{"definition": "   "}]})


def test_zero_entries_raises(tmp_path: Path) -> None:
    spec = DictSpec.from_json(
        {
            "dict_id": "sample-en",
            "name": "Sample English",
            "source_lang": "en",
            "target_lang": "en",
            "version": "1",
            "publisher": "p",
            "license": "l",
            "license_url": "u",
            "attribution": "a",
            "kind": "monolingual",
        }
    )
    with pytest.raises(BuildError, match=r"zero entries"):
        build_bundle([], spec, tmp_path)


def test_read_jsonl_reports_line_number(tmp_path: Path) -> None:
    bad_path = tmp_path / "bad.jsonl"
    bad_path.write_text(
        '{"headword": "ok", "lang": "en", "senses": [{"definition": "x"}]}\n{not json}\n'
    )
    with pytest.raises(BuildError, match=re.escape(f"{bad_path}:2:")):
        list(read_jsonl(bad_path))


# The queries the desktop client runs for every lookup and entry view
# (app/src/core/bundle.cpp). Each must be answered from an index: on an
# 880k-entry bundle a single table scan costs tens of milliseconds.
_CLIENT_QUERIES = (
    "SELECT id FROM entries WHERE headword_norm = 'x'",
    "SELECT entry_id FROM forms WHERE form_norm = 'x'",
    "SELECT id FROM entries WHERE headword_norm >= 'x' AND headword_norm < 'y'",
    "SELECT entry_id FROM forms WHERE form_norm >= 'x' AND form_norm < 'y'",
    "SELECT id, headword, lang, frequency FROM entries WHERE id = 1",
    "SELECT ipa, region FROM pronunciations WHERE entry_id = 1 ORDER BY id",
    "SELECT id, ordinal FROM senses WHERE entry_id = 1 ORDER BY ordinal, id",
    "SELECT text, translation FROM examples WHERE sense_id = 1 ORDER BY ordinal, id",
    "SELECT form, tag FROM forms WHERE entry_id = 1 ORDER BY rowid",
    "SELECT type, target FROM relations WHERE entry_id = 1 ORDER BY rowid",
    "SELECT entry_id FROM fts_map WHERE rowid = 1",
)


@pytest.mark.parametrize("query", _CLIENT_QUERIES)
def test_client_queries_never_scan_a_table(fixtures_dir: Path, tmp_path: Path, query: str) -> None:
    spec = DictSpec.from_json(
        json.loads((fixtures_dir / "sample-en.meta.json").read_text(encoding="utf-8"))
    )
    bundle = build_bundle(read_jsonl(fixtures_dir / "sample-en.jsonl"), spec, tmp_path)
    conn = sqlite3.connect(bundle)
    try:
        plan = [row[3] for row in conn.execute(f"EXPLAIN QUERY PLAN {query}")]
    finally:
        conn.close()
    scans = [step for step in plan if step.startswith("SCAN") and "USING" not in step]
    assert not scans, f"{query}: {plan}"
