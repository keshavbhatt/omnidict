# Third-party components

Nothing is vendored except the Lucide glyphs (data files, not code): every other component below is a system/runtime library, a snap-provided SDK
or runtime, or a package pulled from PyPI at pipeline install time. No new third-party
dependency is added without an ADR in `DOCS/DECISIONS.md` and a row here (ADR-008).

| Component | Version | Licence | How it is obtained | Used by |
|---|---|---|---|---|
| Qt | 6.11.1 | LGPL-3.0 | `kde-qt6-core24-sdk` snap (build), `kf6-core24` snap (runtime) | `app` |
| SQLite | bundled with the runtime | Public domain | system/runtime library; the pipeline uses Python's built-in `sqlite3` module | `app` and `pipeline` |
| ICU | 74 (as shipped by the `kf6-core24` runtime) | Unicode-3.0 | `kf6-core24` runtime snap | `app` |
| zstd | bundled with the runtime (1.5.5 as shipped by the SDK/runtime snaps) | BSD-3-Clause | system/runtime library, found via `app/cmake/FindZstd.cmake` (ADR-015) | `app` (bundle decompression, `core::installBundle`) |
| PyICU | pinned in `pipeline/pyproject.toml` | MIT | PyPI, via `uv` | `pipeline` |
| zstandard | pinned in `pipeline/pyproject.toml`, from M1 | BSD-3-Clause | PyPI, via `uv` | `pipeline` (bundle packaging, from M1) |
| ruff | dev-only | MIT | PyPI, via `uv` | `pipeline` tooling |
| mypy | dev-only | MIT | PyPI, via `uv` | `pipeline` tooling |
| pytest | dev-only | MIT | PyPI, via `uv` | `pipeline` tooling |
| Qt Svg | 6.11.1 | LGPL-3.0 | part of Qt: the same SDK and runtime snaps (ADR-014) | `app` (tinted UI glyphs) |
| Lucide icons | snapshot ba6751a, 2026-09-18 | ISC (Feather-derived ones also MIT) | copied from the rewrite kit's vendored set into `app/src/resources/icons/ui/`, notice in `LICENSE` there (ADR-014) | `app` |
| Qt Network | 6.11.1 | LGPL-3.0 | part of Qt: the same SDK and runtime snaps (ADR-015) | `app` (`services::DictionaryManager`: catalogue and `.odict` downloads) |
| Qt DBus | 6.11.1 | LGPL-3.0 | part of Qt: the same SDK and runtime snaps, the Flatpak runtime, bundled in the AppImage (ADR-019) | `app` (`services::GlobalShortcuts`: the Quick Lookup shortcut) |
| Qt (AppImage build) | 6.11.x | LGPL-3.0 | official Qt installer through `jurplel/install-qt-action` (aqtinstall), bundled into the AppImage with its ICU (ADR-018) | AppImage |
| linuxdeploy, linuxdeploy-plugin-qt | continuous | MIT | downloaded in CI, not shipped (ADR-018) | AppImage build |
| SQLite, zstd, ICU (AppImage) | as in Ubuntu 22.04 (SQLite 3.37, ICU 70) | Public domain, BSD-3-Clause, Unicode-3.0 | Ubuntu 22.04 packages, bundled into the AppImage by linuxdeploy (ADR-018) | AppImage |

## Design mocks only

`DOCS/mocks/mock.js` embeds Lucide glyph paths (ISC licence, copied from the rewrite kit's
vendored set) so the HTML mocks can draw icons. They do not ship in the app.

The app's glyphs are the Lucide row above; the mocks use the same set.
