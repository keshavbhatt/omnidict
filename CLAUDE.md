# Omnidict: project instructions

Omnidict is a cross-platform (Linux first) desktop dictionary app: a Qt 6.11 / C++20 / CMake
client in `app/`, a Python 3.12 data pipeline (package `omnipipe`) in `pipeline/`, and one
canonical `.odict` SQLite bundle per dictionary. `DOCS/` holds the plan (`PLAN.md`), the scope
contract (`FEATURES.md`), decisions (`DECISIONS.md`), the binding coding standards
(`CODING_STANDARDS.md`), and the progress log (`PROGRESS.md`). Conventions come from the
owner's reusable rewrite kit, kept outside this repository (read-only); `DOCS/CODING_STANDARDS.md` is
the adapted, binding version for this repo.

## Build and test

```sh
make fixture   # builds pipeline/out/sample-en/dict.sqlite from tests/fixtures/sample-en.jsonl
make test      # pipeline tests, builds the fixture, builds the app, runs ctest
make lint      # ruff, mypy, clang-format, clang-tidy, shellcheck, reuse
OMNIDICT_CMAKE_ARGS="-DOMNIDICT_BUILD_TESTS=ON -DOMNIDICT_WERROR=ON" scripts/dev-build.sh --tests
scripts/dev-run.sh -- --lookup pipeline/out/sample-en/dict.sqlite perhaps
```

Real dictionaries (in `pipeline/`, see `DOCS/sources.md`; the disk is nearly full, so check
`df -h` before building `wikt-en`, which needs about 2 GB):

```sh
make fetch SOURCE=kaikki          # refresh dumps into sources/kaikki/
make build DICT=wikt-hi-en        # -> out/wikt-hi-en/dict.sqlite
make package DICT=wikt-hi-en      # -> out/publish/dicts/<id>/<version>/{*.odict,manifest.json}
make catalog && make serve        # out/publish/catalog.json on http://localhost:8000
```

Run the app against them with a scratch profile (never the owner's own data):
`scripts/dev-run.sh -- --bundles pipeline/out --profile /tmp/omnidict-profile`. Headless check:
`QT_QPA_PLATFORM=offscreen OMNIDICT_DEBUG_QUERY=perro OMNIDICT_DEBUG_GRAB=shot.png scripts/dev-run.sh -- --bundles pipeline/out`.

`tst_real_bundles` checks every built bundle against `tests/known_headwords.json` and skips
the ones not built.

The dictionary catalogue is fetched from the rolling GitHub release by default
(`https://github.com/keshavbhatt/omnidict/releases/download/dictionaries/catalog.json`, ADR-016;
the owner switched it on 2026-09-29). To test a local catalogue from `make serve`, set
`OMNIDICT_CATALOG_URL=http://localhost:8000/catalog.json`; a cached catalogue from another address
is never reused.

## Standing rules

- No em dashes or en dashes anywhere. No commit or PR attribution lines. Commit style:
  `area: plain sentence` or `area, area: plain sentence`.
- Never mention accounts or licensing in user-facing release text (`CHANGELOG.md`, metainfo
  release notes, the What's new sheet, README store copy); that work is described only in
  commit messages and `DOCS/`. `tst_changelog` enforces this for the changelog. Exception:
  dictionary content licences and attribution (CC BY-SA and so on) are a legal obligation and
  ARE shown to users, in the About screen and per dictionary; they still stay out of
  `CHANGELOG.md` because the changelog guard matches the word "licence"/"license".
- Never change the SQLite schema or manifest fields without bumping `schema_version` and
  updating `DOCS/schema.md`.
- All definition and example markup must pass `omnipipe.html_subset.validate()`; every
  converter has tests.
- Headword normalization must stay identical in Python (`pipeline/omnipipe/normalize.py`) and
  C++ (`app/src/core/normalize.cpp`); both read `tests/normalize_vectors.json`.
- No content source without a documented licence in `DOCS/sources.md`.
- Entry rendering stays client-agnostic (PLAN D12). No `web/` work before M5 is complete.
- No new third-party dependency without an ADR and a `THIRD_PARTY.md` row.
- The snap is built by GitHub Actions only, the Flatpak by Flathub's CI only; never push to a
  Flathub fork or open a Flathub PR without the owner's explicit consent.
- Verify headlessly before asking the owner to look.
- Mocks before screens: every screen or visible UI change first gets an HTML mock in
  `DOCS/mocks/` (see `DOCS/DESIGN.md`) that the owner approves; approval is recorded in
  `DOCS/mocks/index.html` and `DOCS/PROGRESS.md`. No screen is coded from a description alone.
- Publishing stays local: `make publish` and anything else that exposes bundles, the catalogue
  or the app publicly waits until the owner says the feature is implemented, tested, reviewed
  and ready. The source code is public (github.com/keshavbhatt/omnidict, owner, 2026-09-28)
  and the app reads the dictionaries workflow's rolling release (owner, 2026-09-29); the
  workflow runs only when the owner starts it. Store listings and Flathub still wait for the
  owner.
- Run `make test` and `make lint` before declaring a task done.
- Open questions in `DOCS/PLAN.md` section 9 are the owner's: ask, do not assume.
- Release checklist: date the changelog heading, add the metainfo release, bump
  `project(VERSION)` in `app/CMakeLists.txt`, update the snap description.
