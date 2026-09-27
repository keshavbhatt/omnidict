"""Tests for `omnipipe.catalog`."""

from __future__ import annotations

import json
import shutil
import sqlite3
from datetime import UTC, datetime
from pathlib import Path

import pytest

from omnipipe.build import build_bundle, read_jsonl
from omnipipe.catalog import CatalogError, build_catalog, version_key
from omnipipe.package import package_bundle
from omnipipe.schema import DictSpec

_BUILT_AT = datetime(2026, 9, 27, tzinfo=UTC)


def _load_spec(fixtures_dir: Path) -> DictSpec:
    meta_obj = json.loads((fixtures_dir / "sample-en.meta.json").read_text(encoding="utf-8"))
    return DictSpec.from_json(meta_obj)


def _build(fixtures_dir: Path, out_dir: Path) -> Path:
    spec = _load_spec(fixtures_dir)
    entries = list(read_jsonl(fixtures_dir / "sample-en.jsonl"))
    return build_bundle(entries, spec, out_dir, built_at=_BUILT_AT)


def _bump_version(sqlite_path: Path, new_version: str) -> None:
    conn = sqlite3.connect(sqlite_path)
    try:
        conn.execute("UPDATE meta SET value = ? WHERE key = 'version'", (new_version,))
        conn.commit()
    finally:
        conn.close()


def test_build_catalog_keeps_only_highest_version(fixtures_dir: Path, tmp_path: Path) -> None:
    publish_root = tmp_path / "publish"

    sqlite_v1 = _build(fixtures_dir, tmp_path / "build-v1")
    package_bundle(sqlite_v1, publish_root, "http://localhost:8000")

    sqlite_v2 = tmp_path / "build-v2" / "dict.sqlite"
    sqlite_v2.parent.mkdir(parents=True)
    shutil.copyfile(sqlite_v1, sqlite_v2)
    _bump_version(sqlite_v2, "2026.10.1")
    package_bundle(sqlite_v2, publish_root, "http://localhost:8000")

    catalog = build_catalog(publish_root)

    assert catalog["catalog_version"] == 1
    dictionaries = catalog["dictionaries"]
    assert isinstance(dictionaries, list)
    assert len(dictionaries) == 1
    assert dictionaries[0]["dict_id"] == "sample-en"
    assert dictionaries[0]["version"] == "2026.10.1"


def test_build_catalog_generated_at_formatting(fixtures_dir: Path, tmp_path: Path) -> None:
    publish_root = tmp_path / "publish"
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    package_bundle(sqlite_path, publish_root, "http://localhost:8000")

    when = datetime(2026, 9, 27, 12, 30, 45, 123456, tzinfo=UTC)
    catalog = build_catalog(publish_root, generated_at=when)
    assert catalog["generated_at"] == "2026-09-27T12:30:45Z"


def test_build_catalog_writes_file_matching_return_value(
    fixtures_dir: Path, tmp_path: Path
) -> None:
    publish_root = tmp_path / "publish"
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    package_bundle(sqlite_path, publish_root, "http://localhost:8000")

    catalog = build_catalog(publish_root)
    catalog_path = publish_root / "catalog.json"
    assert catalog_path.is_file()
    on_disk = json.loads(catalog_path.read_text(encoding="utf-8"))
    assert on_disk == catalog
    assert catalog_path.read_text(encoding="utf-8").endswith("\n")


def test_build_catalog_missing_odict_raises(fixtures_dir: Path, tmp_path: Path) -> None:
    publish_root = tmp_path / "publish"
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    package_bundle(sqlite_path, publish_root, "http://localhost:8000")

    odict_path = publish_root / "dicts" / "sample-en" / "2026.09.1" / "sample-en.odict"
    odict_path.unlink()

    with pytest.raises(CatalogError):
        build_catalog(publish_root)


def test_build_catalog_size_mismatch_raises(fixtures_dir: Path, tmp_path: Path) -> None:
    publish_root = tmp_path / "publish"
    sqlite_path = _build(fixtures_dir, tmp_path / "build")
    package_bundle(sqlite_path, publish_root, "http://localhost:8000")

    manifest_path = publish_root / "dicts" / "sample-en" / "2026.09.1" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["size_compressed"] = int(manifest["size_compressed"]) + 1
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    with pytest.raises(CatalogError, match="size"):
        build_catalog(publish_root)


def test_build_catalog_empty_publish_tree_raises(tmp_path: Path) -> None:
    publish_root = tmp_path / "publish"
    publish_root.mkdir()
    with pytest.raises(CatalogError):
        build_catalog(publish_root)


def test_version_key_ordering() -> None:
    assert version_key("2026.10.1") > version_key("2026.9.2")
    assert version_key("2026.9.2") > version_key("2026.09.1")
    assert version_key("2026.9.2") == version_key("2026.09.2")


def test_version_key_rejects_non_numeric_parts() -> None:
    with pytest.raises(CatalogError):
        version_key("2026.x.1")
