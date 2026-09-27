# Omnidict

A cross-platform (Linux first) desktop dictionary app. A Qt 6.11 / C++20 client browses a
catalog of dictionaries and looks up words in one consistent, offline SQLite bundle format
(`.odict`) per dictionary; a Python data pipeline (`omnipipe`) converts open dictionary sources
into that format.

## Status

Pre-release, milestone M0 (repo scaffold and the pipeline's core modules). See
`DOCS/ROADMAP.md` for the full milestone plan and `DOCS/FEATURES.md` for the scope contract.

## Layout

```
app/         Qt 6.11 / C++20 client (CMake root is app/, see DECISIONS.md ADR-010)
pipeline/    Python 3.12 data pipeline, package omnipipe
tests/       fixtures and vectors shared by pipeline and app tests
scripts/     dev-build.sh, dev-run.sh, snap-runtime-env.sh
DOCS/        the project plan, coding standards, decisions, progress log
```

## Prerequisites

- `kde-qt6-core24-sdk` and `kf6-core24` snaps (Qt 6.11 build SDK and matching runtime):
  `sudo snap install kde-qt6-core24-sdk kf6-core24`
- CMake and Ninja
- `uv` for the Python pipeline

## Quick start

```sh
make fixture   # builds pipeline/out/sample-en/dict.sqlite from tests/fixtures/sample-en.jsonl
make test      # pipeline tests, the fixture, the app build, and ctest
make lint      # ruff, mypy, clang-format check, reuse lint
scripts/dev-build.sh --tests
scripts/dev-run.sh -- --lookup pipeline/out/sample-en/dict.sqlite perhaps
```

## Documentation

Everything about the project lives under `DOCS/`; start with `DOCS/README.md` for the index
and workflow, and `DOCS/PLAN.md` for the architecture and canonical schema.

Licence: see `LICENSING.md`.
