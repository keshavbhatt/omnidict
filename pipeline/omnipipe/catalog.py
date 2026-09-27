"""Regenerates `catalog.json` from every manifest under a publish tree.

The catalog is the static index the desktop client (and, later, the web
app) fetches to discover available dictionaries (PLAN.md 5.2): no API
server, just a `catalog.json` on a CDN listing the newest manifest for each
`dict_id`. This module never re-hashes `.odict` files: `package_bundle`
already computed and recorded their sha256, and hashing gigabytes of
bundles on every catalog run would make `make catalog` far too slow to run
after every publish. Instead it checks the cheap, still-meaningful things:
required keys present, the `.odict` file exists, and its size matches what
the manifest claims.
"""

from __future__ import annotations

import argparse
import json
import logging
import re
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Final

from omnipipe.package import ODICT_SUFFIX

logger = logging.getLogger(__name__)

CATALOG_VERSION: Final = 1

# Every key a manifest written by `package.package_bundle` carries (PLAN.md 5.1).
_REQUIRED_MANIFEST_KEYS: Final = (
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
)

_VERSION_RE: Final = re.compile(r"^\d+(\.\d+)*$")


class CatalogError(Exception):
    """Raised when the publish tree cannot be turned into a valid catalog."""


def version_key(version: str) -> tuple[int, ...]:
    """Turn a dotted version string into a tuple sortable by numeric value.

    `"2026.09.1"` -> `(2026, 9, 1)`. Leading zeros are insignificant, so
    `"2026.9.2"` and `"2026.09.2"` compare equal. Raises `CatalogError` for
    anything that is not dot-separated non-negative integers.
    """
    if not _VERSION_RE.match(version):
        raise CatalogError(
            f"{version!r}: not a valid version (expected dot-separated non-negative integers)"
        )
    return tuple(int(part) for part in version.split("."))


def _format_generated_at(when: datetime | None) -> str:
    moment = when if when is not None else datetime.now(UTC)
    moment = moment.astimezone(UTC).replace(microsecond=0)
    return moment.strftime("%Y-%m-%dT%H:%M:%SZ")


def _write_json_atomic(path: Path, data: object) -> None:
    tmp_path = path.parent / f"{path.name}.tmp"
    if tmp_path.exists():
        tmp_path.unlink()
    text = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    tmp_path.write_text(text, encoding="utf-8")
    tmp_path.replace(path)


def _load_manifest(path: Path) -> dict[str, object]:
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise CatalogError(f"{path}: cannot read manifest: {exc}") from exc
    try:
        obj = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise CatalogError(f"{path}: invalid JSON: {exc}") from exc
    if not isinstance(obj, dict):
        raise CatalogError(f"{path}: manifest is not a JSON object")

    missing = [key for key in _REQUIRED_MANIFEST_KEYS if key not in obj]
    if missing:
        raise CatalogError(f"{path}: manifest missing required key(s): {', '.join(missing)}")
    return obj


def _int_field(manifest: dict[str, object], key: str, manifest_path: Path) -> int:
    """Read an integer field out of a manifest dict, typed as `object`
    because manifests are plain JSON-shaped dicts (mypy strict needs the
    isinstance narrowing; a bad manifest is a `CatalogError`, not a crash).
    """
    value = manifest[key]
    if isinstance(value, bool) or not isinstance(value, int):
        raise CatalogError(f"{manifest_path}: manifest {key!r}: expected integer, got {value!r}")
    return value


def _check_odict_matches(manifest_path: Path, manifest: dict[str, object]) -> None:
    dict_id = str(manifest["dict_id"])
    version = str(manifest["version"])
    version_dir = manifest_path.parent
    dict_dir = version_dir.parent

    if version_dir.name != version:
        raise CatalogError(
            f"{manifest_path}: directory {version_dir.name!r} does not match "
            f"manifest version {version!r}"
        )
    if dict_dir.name != dict_id:
        raise CatalogError(
            f"{manifest_path}: directory {dict_dir.name!r} does not match "
            f"manifest dict_id {dict_id!r}"
        )

    odict_path = version_dir / f"{dict_id}{ODICT_SUFFIX}"
    if not odict_path.is_file():
        raise CatalogError(f"{manifest_path}: missing bundle file {odict_path}")

    expected_size = _int_field(manifest, "size_compressed", manifest_path)
    actual_size = odict_path.stat().st_size
    if actual_size != expected_size:
        raise CatalogError(
            f"{odict_path}: size {actual_size} does not match "
            f"manifest size_compressed {expected_size}"
        )


def build_catalog(
    publish_root: Path,
    *,
    generated_at: datetime | None = None,
) -> dict[str, object]:
    """Rebuild `publish_root/catalog.json` from every manifest on disk.

    Walks `publish_root/dicts/*/*/manifest.json`, validates each one, keeps
    only the highest version per `dict_id` (by `version_key`), and writes
    the result sorted by `dict_id`. Raises `CatalogError` if zero manifests
    are found or any manifest fails validation.
    """
    manifest_paths = sorted(publish_root.glob("dicts/*/*/manifest.json"))
    if not manifest_paths:
        raise CatalogError(f"{publish_root}: no manifest.json files found under dicts/*/*/")

    best_by_id: dict[str, tuple[tuple[int, ...], dict[str, object]]] = {}
    for manifest_path in manifest_paths:
        manifest = _load_manifest(manifest_path)
        _check_odict_matches(manifest_path, manifest)

        dict_id = str(manifest["dict_id"])
        version = str(manifest["version"])
        key = version_key(version)

        current = best_by_id.get(dict_id)
        if current is None or key > current[0]:
            if current is not None:
                logger.warning(
                    "catalog: skipping older version %s of %s (keeping %s)",
                    current[1]["version"],
                    dict_id,
                    version,
                )
            best_by_id[dict_id] = (key, manifest)
        else:
            logger.warning(
                "catalog: skipping older version %s of %s (keeping %s)",
                version,
                dict_id,
                current[1]["version"],
            )

    dictionaries = [best_by_id[dict_id][1] for dict_id in sorted(best_by_id)]
    for manifest in dictionaries:
        logger.info("catalog: including %s %s", manifest["dict_id"], manifest["version"])

    catalog: dict[str, object] = {
        "catalog_version": CATALOG_VERSION,
        "generated_at": _format_generated_at(generated_at),
        "dictionaries": dictionaries,
    }
    _write_json_atomic(publish_root / "catalog.json", catalog)
    return catalog


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.catalog")
    parser.add_argument("--publish", dest="publish_root", required=True, type=Path)
    parser.add_argument("--generated-at", dest="generated_at", default=None)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """CLI entry point: regenerate `catalog.json` for a publish tree."""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(sys.argv[1:] if argv is None else argv)

    try:
        generated_at = (
            datetime.strptime(args.generated_at, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)
            if args.generated_at is not None
            else None
        )
        catalog = build_catalog(args.publish_root, generated_at=generated_at)
    except (CatalogError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    catalog_path = args.publish_root / "catalog.json"
    dictionaries = catalog["dictionaries"]
    assert isinstance(dictionaries, list)
    print(f"catalog {catalog_path}: {len(dictionaries)} dictionaries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
