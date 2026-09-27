"""Turns a built `dict.sqlite` into a distributable `.odict` bundle.

An `.odict` file is a zstd-compressed copy of a `dict.sqlite` produced by
`build.py`, published alongside a `manifest.json` describing it (PLAN.md
5.1). This module never re-derives content: everything in the manifest
comes from the bundle's `meta` table or from measuring the files on disk.

Bundles can be hundreds of megabytes, so every pass over file contents here
is streaming: nothing reads a whole `dict.sqlite` or `.odict` into memory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import sqlite3
import sys
from collections.abc import Mapping
from pathlib import Path
from typing import Final

import zstandard

logger = logging.getLogger(__name__)

ODICT_SUFFIX: Final = ".odict"

# The 12 PLAN.md 5.1 keys every bundle's `meta` table must carry, regardless
# of whether it was hand-written (the test fixture) or produced by a
# converter. `source_converter` and `source_dump_date` are checked
# separately: they are optional (PLAN.md 5.1 `source`).
_REQUIRED_META_KEYS: Final = (
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
)

_CHUNK_SIZE: Final = 1024 * 1024  # 1 MiB, used for every streaming pass below.


class PackageError(Exception):
    """Raised when a bundle cannot be read, packaged, or verified."""


def _ro_uri(path: Path) -> str:
    """Build a read-only `file:` URI for `sqlite3.connect(..., uri=True)`.

    Uses `Path.as_uri()` to percent-encode the path the way SQLite expects,
    so odd paths (spaces, unicode) do not break the URI, then appends the
    `mode=ro` query parameter. `resolve()` makes the path absolute without
    requiring it to exist yet, so a missing file still produces a valid URI
    and fails later, inside SQLite, with a clear error.
    """
    return f"{path.resolve().as_uri()}?mode=ro"


def read_meta(sqlite_path: Path) -> dict[str, str]:
    """Read and validate the `meta` table of a `dict.sqlite` bundle.

    Opens the file strictly read-only. Raises `PackageError` if the file is
    missing, is not a valid SQLite database, the meta table lacks any of
    the 12 required PLAN.md keys, or `schema_version` is not `"1"`.
    """
    uri = _ro_uri(sqlite_path)
    try:
        conn = sqlite3.connect(uri, uri=True)
        try:
            rows = conn.execute("SELECT key, value FROM meta").fetchall()
        finally:
            conn.close()
    except sqlite3.Error as exc:
        raise PackageError(f"{sqlite_path}: cannot read meta table: {exc}") from exc

    meta = {str(key): str(value) for key, value in rows}
    missing = [key for key in _REQUIRED_META_KEYS if key not in meta]
    if missing:
        raise PackageError(
            f"{sqlite_path}: meta table missing required key(s): {', '.join(missing)}"
        )
    if meta["schema_version"] != "1":
        raise PackageError(
            f"{sqlite_path}: unsupported schema_version {meta['schema_version']!r} (expected '1')"
        )
    return meta


def _sha256_file(path: Path) -> str:
    """Lowercase hex sha256 of a file, computed in 1 MiB chunks."""
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(_CHUNK_SIZE), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def _write_json_atomic(path: Path, data: Mapping[str, object]) -> None:
    """Write `data` as pretty JSON to `path`, replacing it atomically."""
    tmp_path = path.parent / f"{path.name}.tmp"
    if tmp_path.exists():
        tmp_path.unlink()
    text = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    tmp_path.write_text(text, encoding="utf-8")
    tmp_path.replace(path)


def _kind_of(meta: Mapping[str, str]) -> str:
    kind = meta.get("kind")
    if kind:
        return kind
    return "bilingual" if meta["source_lang"] != meta["target_lang"] else "monolingual"


def _source_of(meta: Mapping[str, str]) -> dict[str, str]:
    source: dict[str, str] = {}
    if "source_converter" in meta:
        source["converter"] = meta["source_converter"]
    if "source_dump_date" in meta:
        source["dump_date"] = meta["source_dump_date"]
    return source


def package_bundle(
    sqlite_path: Path,
    publish_root: Path,
    base_url: str,
    *,
    level: int = 19,
) -> dict[str, object]:
    """Compress `sqlite_path` into `publish_root` and write its manifest.

    Output layout: `publish_root/dicts/<dict_id>/<version>/<dict_id>.odict`
    and a `manifest.json` next to it (PLAN.md 5.1). Compression is streaming
    zstd with checksums and the decompressed size embedded in the frame, so
    real (hundreds-of-MB) bundles never need to be held in memory. Packaging
    the same `dict.sqlite` twice produces a byte-identical `.odict` and
    manifest: zstd's output at a fixed level is deterministic even with
    multiple compression threads (`threads=-1`), which the test suite
    verifies.
    """
    meta = read_meta(sqlite_path)
    dict_id = meta["dict_id"]
    version = meta["version"]

    version_dir = publish_root / "dicts" / dict_id / version
    version_dir.mkdir(parents=True, exist_ok=True)
    odict_path = version_dir / f"{dict_id}{ODICT_SUFFIX}"
    tmp_path = version_dir / f"{dict_id}{ODICT_SUFFIX}.tmp"
    if tmp_path.exists():
        tmp_path.unlink()

    compressor = zstandard.ZstdCompressor(
        level=level, threads=-1, write_checksum=True, write_content_size=True
    )
    try:
        with sqlite_path.open("rb") as src, tmp_path.open("wb") as dst:
            compressor.copy_stream(src, dst, read_size=_CHUNK_SIZE, write_size=_CHUNK_SIZE)
    except (OSError, zstandard.ZstdError) as exc:
        tmp_path.unlink(missing_ok=True)
        raise PackageError(f"{sqlite_path}: compression failed: {exc}") from exc
    tmp_path.replace(odict_path)

    size_installed = sqlite_path.stat().st_size
    size_compressed = odict_path.stat().st_size
    sha256 = _sha256_file(odict_path)
    url = f"{base_url.rstrip('/')}/dicts/{dict_id}/{version}/{dict_id}{ODICT_SUFFIX}"

    # Key order matches PLAN.md 5.1 exactly; the manifest is read by humans too.
    manifest: dict[str, object] = {
        "dict_id": dict_id,
        "name": meta["name"],
        "source_lang": meta["source_lang"],
        "target_lang": meta["target_lang"],
        "kind": _kind_of(meta),
        "version": version,
        "schema_version": int(meta["schema_version"]),
        "publisher": meta["publisher"],
        "license": meta["license"],
        "license_url": meta["license_url"],
        "attribution": meta["attribution"],
        "entry_count": int(meta["entry_count"]),
        "size_compressed": size_compressed,
        "size_installed": size_installed,
        "sha256": sha256,
        "url": url,
        "built_at": meta["built_at"],
        "source": _source_of(meta),
    }

    _write_json_atomic(version_dir / "manifest.json", manifest)
    logger.info("packaged %s (%d bytes, sha256 %s)", odict_path, size_compressed, sha256[:12])
    return manifest


def _int_field(manifest: Mapping[str, object], key: str) -> int:
    """Read an integer field out of a manifest mapping, typed as `object`
    because manifests are plain JSON-shaped dicts (mypy strict needs the
    isinstance narrowing; a bad manifest is a `PackageError`, not a crash).
    """
    value = manifest[key]
    if isinstance(value, bool) or not isinstance(value, int):
        raise PackageError(f"manifest {key!r}: expected integer, got {value!r}")
    return value


def verify_package(odict_path: Path, manifest: Mapping[str, object]) -> None:
    """Check an `.odict` file against its manifest, the way an installing
    client does (PLAN.md 5.3): sha256 and compressed size streamed from the
    file as-is, then decompressed size streamed through the zstd decoder.
    Nothing is written to disk. Raises `PackageError` naming the specific
    mismatch.
    """
    expected_sha256 = str(manifest["sha256"])
    expected_size_compressed = _int_field(manifest, "size_compressed")
    expected_size_installed = _int_field(manifest, "size_installed")

    hasher = hashlib.sha256()
    size_compressed = 0
    with odict_path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(_CHUNK_SIZE), b""):
            hasher.update(chunk)
            size_compressed += len(chunk)

    if size_compressed != expected_size_compressed:
        raise PackageError(
            f"{odict_path}: size_compressed mismatch: "
            f"expected {expected_size_compressed}, got {size_compressed}"
        )
    digest = hasher.hexdigest()
    if digest != expected_sha256:
        raise PackageError(
            f"{odict_path}: sha256 mismatch: expected {expected_sha256}, got {digest}"
        )

    decompressor = zstandard.ZstdDecompressor()
    size_installed = 0
    try:
        with odict_path.open("rb") as handle, decompressor.stream_reader(handle) as reader:
            while True:
                chunk = reader.read(_CHUNK_SIZE)
                if not chunk:
                    break
                size_installed += len(chunk)
    except zstandard.ZstdError as exc:
        raise PackageError(f"{odict_path}: decompression failed: {exc}") from exc

    if size_installed != expected_size_installed:
        raise PackageError(
            f"{odict_path}: size_installed mismatch: "
            f"expected {expected_size_installed}, got {size_installed}"
        )


def decompress_bundle(odict_path: Path, out_path: Path) -> Path:
    """Stream-decompress `odict_path` to `out_path`, replacing it atomically.

    Useful for tests and local checks; the real client does the equivalent
    of this as part of the install flow (PLAN.md 5.3 step 3).
    """
    out_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = out_path.parent / f"{out_path.name}.tmp"
    if tmp_path.exists():
        tmp_path.unlink()

    decompressor = zstandard.ZstdDecompressor()
    try:
        with odict_path.open("rb") as src, tmp_path.open("wb") as dst:
            decompressor.copy_stream(src, dst, read_size=_CHUNK_SIZE, write_size=_CHUNK_SIZE)
    except (OSError, zstandard.ZstdError) as exc:
        tmp_path.unlink(missing_ok=True)
        raise PackageError(f"{odict_path}: decompression failed: {exc}") from exc
    tmp_path.replace(out_path)
    return out_path


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.package")
    parser.add_argument("--in", dest="in_path", required=True, type=Path)
    parser.add_argument("--publish", dest="publish_root", required=True, type=Path)
    parser.add_argument("--base-url", dest="base_url", default="http://localhost:8000")
    parser.add_argument("--level", dest="level", type=int, default=19)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """CLI entry point: package a `dict.sqlite` into a publish tree."""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(sys.argv[1:] if argv is None else argv)

    try:
        manifest = package_bundle(args.in_path, args.publish_root, args.base_url, level=args.level)
    except PackageError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    dict_id = manifest["dict_id"]
    version = manifest["version"]
    odict_path = (
        args.publish_root / "dicts" / str(dict_id) / str(version) / f"{dict_id}{ODICT_SUFFIX}"
    )
    sha256 = str(manifest["sha256"])
    print(f"packaged {odict_path} ({manifest['size_compressed']} bytes, sha256 {sha256[:12]}...)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
