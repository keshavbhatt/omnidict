"""Tests for `omnipipe.package`."""

from __future__ import annotations

import dataclasses
import hashlib
import json
import sqlite3
from datetime import UTC, datetime
from pathlib import Path

import pytest

from omnipipe.build import build_bundle, read_jsonl
from omnipipe.package import (
    PackageError,
    decompress_bundle,
    package_bundle,
    read_meta,
    verify_package,
)
from omnipipe.schema import SCHEMA_VERSION, DictSpec

_BUILT_AT = datetime(2026, 9, 27, tzinfo=UTC)

_MANIFEST_KEY_ORDER = [
    "dict_id",
    "name",
    "source_lang",
    "target_lang",
    "kind",
    "version",
    "schema_version",
    "publisher",
    "license",
    "license_url",
    "attribution",
    "entry_count",
    "size_compressed",
    "size_installed",
    "sha256",
    "url",
    "built_at",
    "source",
]


def _load_spec(fixtures_dir: Path) -> DictSpec:
    meta_obj = json.loads((fixtures_dir / "sample-en.meta.json").read_text(encoding="utf-8"))
    return DictSpec.from_json(meta_obj)


def _build(fixtures_dir: Path, out_dir: Path) -> Path:
    spec = _load_spec(fixtures_dir)
    entries = list(read_jsonl(fixtures_dir / "sample-en.jsonl"))
    return build_bundle(entries, spec, out_dir, built_at=_BUILT_AT)


def _odict_path(publish_root: Path) -> Path:
    return publish_root / "dicts" / "sample-en" / "2026.09.1" / "sample-en.odict"


def test_package_bundle_layout_and_manifest(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    publish_root = tmp_path / "publish"

    manifest = package_bundle(sqlite_path, publish_root, "http://localhost:8000")

    version_dir = publish_root / "dicts" / "sample-en" / "2026.09.1"
    odict_path = version_dir / "sample-en.odict"
    manifest_path = version_dir / "manifest.json"
    assert odict_path.is_file()
    assert manifest_path.is_file()

    assert list(manifest.keys()) == _MANIFEST_KEY_ORDER
    assert manifest["dict_id"] == "sample-en"
    assert manifest["name"] == "Sample English"
    assert manifest["kind"] == "monolingual"
    assert manifest["version"] == "2026.09.1"
    assert manifest["schema_version"] == SCHEMA_VERSION
    assert manifest["entry_count"] == 14
    assert manifest["size_installed"] == sqlite_path.stat().st_size
    assert manifest["size_compressed"] == odict_path.stat().st_size
    assert manifest["sha256"] == hashlib.sha256(odict_path.read_bytes()).hexdigest()
    assert manifest["source"] == {}
    assert manifest["url"] == "http://localhost:8000/dicts/sample-en/2026.09.1/sample-en.odict"

    on_disk = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert on_disk == manifest
    assert manifest_path.read_text(encoding="utf-8").endswith("\n")


def test_package_bundle_base_url_without_trailing_slash(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    manifest = package_bundle(sqlite_path, tmp_path / "publish", "http://localhost:8000")
    assert manifest["url"] == "http://localhost:8000/dicts/sample-en/2026.09.1/sample-en.odict"


def test_package_bundle_base_url_with_trailing_slash(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    manifest = package_bundle(sqlite_path, tmp_path / "publish", "http://localhost:8000/")
    assert manifest["url"] == "http://localhost:8000/dicts/sample-en/2026.09.1/sample-en.odict"


def test_package_bundle_with_source_metadata(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    conn = sqlite3.connect(sqlite_path)
    try:
        conn.executemany(
            "INSERT INTO meta (key, value) VALUES (?, ?)",
            [("source_converter", "kaikki"), ("source_dump_date", "2026-09-20")],
        )
        conn.commit()
    finally:
        conn.close()

    manifest = package_bundle(sqlite_path, tmp_path / "publish", "http://localhost:8000")
    assert manifest["source"] == {"converter": "kaikki", "dump_date": "2026-09-20"}


def test_decompress_bundle_round_trips(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    publish_root = tmp_path / "publish"
    package_bundle(sqlite_path, publish_root, "http://localhost:8000")

    out_path = tmp_path / "roundtrip" / "dict.sqlite"
    result = decompress_bundle(_odict_path(publish_root), out_path)
    assert result == out_path
    assert out_path.read_bytes() == sqlite_path.read_bytes()


def test_package_bundle_is_deterministic(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    publish_a = tmp_path / "publish_a"
    publish_b = tmp_path / "publish_b"

    manifest_a = package_bundle(sqlite_path, publish_a, "http://localhost:8000")
    manifest_b = package_bundle(sqlite_path, publish_b, "http://localhost:8000")

    assert _odict_path(publish_a).read_bytes() == _odict_path(publish_b).read_bytes()
    assert manifest_a == manifest_b


def test_verify_package_passes(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    publish_root = tmp_path / "publish"
    manifest = package_bundle(sqlite_path, publish_root, "http://localhost:8000")

    verify_package(_odict_path(publish_root), manifest)


def test_verify_package_detects_corrupted_file(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    publish_root = tmp_path / "publish"
    manifest = package_bundle(sqlite_path, publish_root, "http://localhost:8000")

    odict_path = _odict_path(publish_root)
    data = bytearray(odict_path.read_bytes())
    midpoint = len(data) // 2
    data[midpoint] ^= 0xFF
    odict_path.write_bytes(bytes(data))

    with pytest.raises(PackageError):
        verify_package(odict_path, manifest)


def test_verify_package_detects_size_installed_mismatch(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    publish_root = tmp_path / "publish"
    manifest = package_bundle(sqlite_path, publish_root, "http://localhost:8000")

    size_installed = manifest["size_installed"]
    assert isinstance(size_installed, int)
    bad_manifest: dict[str, object] = dict(manifest)
    bad_manifest["size_installed"] = size_installed + 1

    with pytest.raises(PackageError, match="size_installed"):
        verify_package(_odict_path(publish_root), bad_manifest)


def test_read_meta_missing_file_raises(tmp_path: Path) -> None:
    with pytest.raises(PackageError):
        read_meta(tmp_path / "does-not-exist.sqlite")


def test_read_meta_non_database_file_raises(tmp_path: Path) -> None:
    bad_path = tmp_path / "not-a-database.sqlite"
    bad_path.write_text("this is plain text, not sqlite", encoding="utf-8")

    with pytest.raises(PackageError):
        read_meta(bad_path)


def test_read_meta_missing_required_key_raises(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    conn = sqlite3.connect(sqlite_path)
    try:
        conn.execute("DELETE FROM meta WHERE key = 'attribution'")
        conn.commit()
    finally:
        conn.close()

    with pytest.raises(PackageError, match="attribution"):
        read_meta(sqlite_path)


def test_read_meta_wrong_schema_version_raises(fixtures_dir: Path, tmp_path: Path) -> None:
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    conn = sqlite3.connect(sqlite_path)
    try:
        conn.execute(
            "UPDATE meta SET value = ? WHERE key = 'schema_version'", (str(SCHEMA_VERSION + 1),)
        )
        conn.commit()
    finally:
        conn.close()

    with pytest.raises(PackageError, match="schema_version"):
        read_meta(sqlite_path)


def test_optional_language_names_and_source_url_reach_the_manifest(
    fixtures_dir: Path, tmp_path: Path
) -> None:
    spec = dataclasses.replace(
        _load_spec(fixtures_dir),
        source_lang_name="English",
        target_lang_name="English",
        source_url="https://example.org/sample-en.tar.xz",
    )
    entries = list(read_jsonl(fixtures_dir / "sample-en.jsonl"))
    sqlite_path = build_bundle(entries, spec, tmp_path / "build", built_at=_BUILT_AT)
    manifest = package_bundle(sqlite_path, tmp_path / "publish", "http://localhost:8000")
    keys = list(manifest.keys())
    assert keys[keys.index("target_lang") + 1 : keys.index("kind")] == [
        "source_lang_name",
        "target_lang_name",
    ]
    assert manifest["source_lang_name"] == "English"
    source = manifest["source"]
    assert isinstance(source, dict)
    assert source["url"] == "https://example.org/sample-en.tar.xz"


def test_dict_spec_round_trips_the_optional_fields() -> None:
    obj: dict[str, object] = {
        "dict_id": "wikt-ang-en",
        "name": "Old English-English (Wiktionary)",
        "source_lang": "ang",
        "target_lang": "en",
        "version": "2026.09.1",
        "publisher": "Wiktionary contributors",
        "license": "CC-BY-SA-4.0",
        "license_url": "https://creativecommons.org/licenses/by-sa/4.0/",
        "attribution": "Wiktionary",
        "kind": "bilingual",
        "source_lang_name": "Old English",
        "target_lang_name": "English",
    }
    spec = DictSpec.from_json(obj)
    assert spec.source_lang_name == "Old English"
    assert spec.source_url == ""
    assert spec.to_json() == obj  # empty optional fields are left out
