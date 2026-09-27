# DOCS: how this project is tracked

Everything about Omnidict lives here. If it is not in `DOCS/`, it did not happen.

| File | What it is |
|---|---|
| `PLAN.md` | The project plan: architecture, canonical schema, milestones. Source of truth for scope. |
| `FEATURES.md` | Feature list with the decision per row. The scope contract. |
| `ROADMAP.md` | Milestones with exit criteria. |
| `PROGRESS.md` | Session log, newest first. |
| `CODING_STANDARDS.md` | Binding Qt 6 / C++20 and Python rules (from the rewrite kit, adapted). |
| `DECISIONS.md` | Architecture Decision Records, append-only. |
| `LESSONS.md` | Rules inherited from the kit's PLAYBOOK that apply to a Qt Widgets desktop app. |
| `schema.md` | Canonical dictionary bundle schema reference, generated from `PLAN.md` section 4. |
| `sources.md` | Per-source notes: format, licence, dump URL, status. |

## Workflow

1. Before coding a feature: its row in `FEATURES.md` must say `KEEP`.
2. While coding: follow `CODING_STANDARDS.md`; new architectural choices get an ADR in `DECISIONS.md`.
3. After coding: update the row's Status in `FEATURES.md`, add a `PROGRESS.md` entry, run the tests.
4. Commit style: `area: plain sentence` or `area, area: plain sentence`, for example:
   - `core: Bundle opens a dictionary and looks up headwords`
   - `pipeline: html_subset sanitizer with tests`

   One logical change per commit, each commit builds and passes. No attribution or
   Co-Authored-By trailers.

## Build and run (dev)

```sh
sudo snap install kde-qt6-core24-sdk kf6-core24   # once
make fixture                                      # builds pipeline/out/sample-en/dict.sqlite
make test                                         # pipeline tests, fixture, app build, ctest
make lint                                         # ruff, mypy, clang-format check, reuse lint
OMNIDICT_CMAKE_ARGS="-DOMNIDICT_BUILD_TESTS=ON -DOMNIDICT_WERROR=ON" scripts/dev-build.sh --tests
scripts/dev-run.sh -- --lookup pipeline/out/sample-en/dict.sqlite perhaps
QT_LOGGING_RULES="omnidict.*.debug=true" scripts/dev-run.sh
```
