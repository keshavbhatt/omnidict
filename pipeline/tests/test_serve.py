"""M1 acceptance in miniature: a publish tree served over HTTP is installable.

Builds and packages the sample bundle, serves the publish tree on a loopback
port (the same way `make serve` does), then plays the client's part of the
install flow (PLAN.md 5.3): read catalog.json, download the bundle from its
manifest URL, verify size and sha256, decompress, open and query it. Loopback
only; nothing leaves the machine.
"""

from __future__ import annotations

import functools
import json
import sqlite3
import threading
import urllib.request
from collections.abc import Iterator
from datetime import UTC, datetime
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

from omnipipe.build import build_bundle, read_jsonl
from omnipipe.catalog import build_catalog
from omnipipe.package import decompress_bundle, package_bundle, verify_package
from omnipipe.schema import DictSpec


class _QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        pass


@pytest.fixture
def served_tree(tmp_path: Path) -> Iterator[tuple[Path, str]]:
    root = tmp_path / "publish"
    root.mkdir()
    handler = functools.partial(_QuietHandler, directory=str(root))
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield root, f"http://127.0.0.1:{server.server_address[1]}"
    finally:
        server.shutdown()
        server.server_close()


def test_a_served_catalog_leads_to_a_verified_openable_bundle(
    fixtures_dir: Path, tmp_path: Path, served_tree: tuple[Path, str]
) -> None:
    root, base_url = served_tree
    spec = DictSpec.from_json(
        json.loads((fixtures_dir / "sample-en.meta.json").read_text(encoding="utf-8"))
    )
    sqlite_path = build_bundle(
        read_jsonl(fixtures_dir / "sample-en.jsonl"),
        spec,
        tmp_path / "build",
        built_at=datetime(2026, 9, 27, tzinfo=UTC),
    )
    package_bundle(sqlite_path, root, base_url)
    build_catalog(root)

    with urllib.request.urlopen(f"{base_url}/catalog.json", timeout=10) as response:
        catalog = json.loads(response.read())
    assert catalog["catalog_version"] == 1
    [manifest] = catalog["dictionaries"]
    assert manifest["dict_id"] == "sample-en"

    download = tmp_path / "download.odict"
    with urllib.request.urlopen(manifest["url"], timeout=10) as response:
        download.write_bytes(response.read())
    verify_package(download, manifest)

    installed = decompress_bundle(download, tmp_path / "installed" / "dict.sqlite")
    assert installed.read_bytes() == sqlite_path.read_bytes()
    conn = sqlite3.connect(installed)
    try:
        meta = dict(conn.execute("SELECT key, value FROM meta").fetchall())
        found = conn.execute(
            "SELECT headword FROM entries WHERE headword_norm = ?", ("perhaps",)
        ).fetchall()
    finally:
        conn.close()
    assert meta["dict_id"] == manifest["dict_id"]
    assert meta["version"] == manifest["version"]
    assert found == [("perhaps",)]
