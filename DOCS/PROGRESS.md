# Progress

Newest first. One entry per working session.

## Milestone status

| Milestone | Status |
|---|---|
| M0 Scaffold | done locally; CI not yet run (no remote) |
| M1 Kaikki + packaging + catalog | todo |
| M2 Qt client (search, entry view, app shell) | todo |
| M3 Manage dictionaries + downloader + About | todo |
| M4 More converters, real catalog | todo |
| M5 Packaging | todo |
| M6 Web app | todo |

## 2026-09-27 - M0 delivered

- Pipeline (`omnipipe`): canonical schema dataclasses with strict validation, restricted HTML
  sanitizer, ICU normalization and collation keys, `build.py` (JSONL to `dict.sqlite`, exact
  PLAN 4.1 DDL, quality gates fail the build on any stripped markup or empty preview),
  `Converter` contract. 139 pytest cases; ruff, ruff format and `mypy --strict` clean.
- Shared contract: `tests/normalize_vectors.json` (44 vectors, 17 scripts). The C++ side (ICU
  74 from the SDK) produces exactly what the pipeline (PyICU 2.16.2 on ICU 78.2) produces.
  Worth knowing: Thai `น้ำ` does not round-trip byte-for-byte (SARA AM is a composition
  exclusion) and Devanagari nukta letters stay decomposed; both are Unicode behaviour and are
  recorded in the vectors.
- App core: `SqliteDb` RAII wrapper, `normalizeHeadword`, `Bundle` (metadata checks, exact
  lookup through headwords and forms, prefix search, full-text search, full entry load),
  `Result<T>`, logging categories, the kit's changelog module. `omnidict --lookup <bundle>
  <word>` is the headless check. Tests: `tst_bundle` (15 test functions on the pipeline-built fixture,
  including refusing a newer schema and missing metadata), `tst_normalize`, `tst_changelog`.
  Built with warnings as errors; clang-format and clang-tidy clean.
- Dev build: the SDK carries SQLite's headers but only a dangling `libsqlite3.so`; the library
  is linked from the core24 base through the runtime farm so the RPATH never reaches core24's
  glibc (`scripts/dev-build.sh`).
- App icon and brand colours from the `flathub-icon` recipe (ADR-011), installed under
  `share/icons/hicolor` by CMake on Linux.
- CI: `.github/workflows/ci.yml` (pipeline lint and tests, fixture artifact, app build and
  ctest on the snap SDK, pinned clang-format 22.1.1 and clang-tidy 22.1.8, shellcheck, REUSE).
  Passes actionlint; not yet run on GitHub because the repository has no remote.
- Licensing module licence text deferred to M3 with the module's code: REUSE rejects a licence
  file that no file uses. The REUSE annotation reserving the module's paths stays.
- Verified: `make test`, `make lint`, headless lookups (exact, prefix suggestion, not found).

## 2026-09-27 - Project started

- Project started: Omnidict, a cross-platform (Linux first) desktop dictionary app, Qt 6.11 /
  C++20 / CMake client in `app/`, Python 3.12 pipeline (`omnipipe`) in `pipeline/`, one
  canonical `.odict` SQLite bundle per dictionary.
- Owner decisions recorded: name Omnidict everywhere (binary `omnidict`, namespace
  `omnidict::<layer>`, CMake prefix `OMNIDICT_`/`omnidict_`, logging category root
  `omnidict.`); identity `com.ktechpit.omnidict`, org `ktechpit`, domain `ktechpit.com`,
  developer Keshav Bhatt; pipeline package `omnipipe`; bundle extension `.odict`; UI is Qt
  Widgets, not QML; C++ style follows the rewrite kit (snake_case files, no `.ui`, layered
  static libraries); Qt 6.11 from the `kde-qt6-core24-sdk` snap, run against `kf6-core24`;
  licence is GPL-3.0-or-later plus the Ktechpit licensing module under its own licence (REUSE
  compliant); pipeline tooling is uv, Python 3.12, ruff, mypy --strict, pytest; scope for now
  is the scaffold plus milestone M0; version starts at 0.1.0.
- Repository scaffold and documentation written from the rewrite kit's templates: `DOCS/PLAN.md`
  updated in place for the Omnidict rename and the kit's layout/naming conventions;
  `DOCS/README.md`, `DOCS/CODING_STANDARDS.md`, `DOCS/DECISIONS.md` (ADR-001 through ADR-010),
  `DOCS/FEATURES.md`, `DOCS/ROADMAP.md`, `DOCS/LESSONS.md`, `DOCS/schema.md`, `DOCS/sources.md`;
  root `CLAUDE.md`, `README.md`, `CHANGELOG.md`, `LICENSING.md`, `REUSE.toml`, `THIRD_PARTY.md`,
  and `LICENSES/`.
- Open: everything in `DOCS/PLAN.md` section 9 that is not yet resolved (CDN/object store
  provider, spell-suggestion implementation, whether to bundle an initial English dictionary,
  telemetry confirmation).
