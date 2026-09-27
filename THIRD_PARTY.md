# Third-party components

Nothing is vendored: every component below is a system/runtime library, a snap-provided SDK
or runtime, or a package pulled from PyPI at pipeline install time. No new third-party
dependency is added without an ADR in `DOCS/DECISIONS.md` and a row here (ADR-008).

| Component | Version | Licence | How it is obtained | Used by |
|---|---|---|---|---|
| Qt | 6.11.1 | LGPL-3.0 | `kde-qt6-core24-sdk` snap (build), `kf6-core24` snap (runtime) | `app` |
| SQLite | bundled with the runtime | Public domain | system/runtime library; the pipeline uses Python's built-in `sqlite3` module | `app` and `pipeline` |
| ICU | 74 (as shipped by the `kf6-core24` runtime) | Unicode-3.0 | `kf6-core24` runtime snap | `app` |
| zstd | from M3 | BSD-3-Clause | system/runtime library | `app` (bundle decompression, from M3) |
| PyICU | pinned in `pipeline/pyproject.toml` | MIT | PyPI, via `uv` | `pipeline` |
| zstandard | pinned in `pipeline/pyproject.toml`, from M1 | BSD-3-Clause | PyPI, via `uv` | `pipeline` (bundle packaging, from M1) |
| ruff | dev-only | MIT | PyPI, via `uv` | `pipeline` tooling |
| mypy | dev-only | MIT | PyPI, via `uv` | `pipeline` tooling |
| pytest | dev-only | MIT | PyPI, via `uv` | `pipeline` tooling |

## Design mocks only

`DOCS/mocks/mock.js` embeds Lucide glyph paths (ISC licence, copied from the rewrite kit's
vendored set) so the HTML mocks can draw icons. They do not ship in the app.

## Planned, not yet added

Lucide icons (ISC) for the app itself, with their licence notice recorded here when the app
starts using them.
