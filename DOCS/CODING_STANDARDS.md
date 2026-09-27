# Coding Standards: Omnidict (Qt 6 / C++20, and the Python pipeline)

These are the rules the project follows. They are binding for every commit. When a rule is
violated for a good reason, the reason goes in a code comment and (if architectural) in
`DOCS/DECISIONS.md`.

This document is adapted from the rewrite kit's `CODING_STANDARDS.md` (`rewrite-kit/reference/red-docs/CODING_STANDARDS.md`),
with the WebEngine-specific rules removed (Omnidict has no web view) and a Python section
added for `pipeline/`. The goal is the same: **small, boring, testable code**.

---

## 1. Language & toolchain

| Item | Rule |
|---|---|
| Language | C++20 (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, no extensions) |
| Qt | Qt 6.11 minimum, everywhere this document or `PLAN.md` mentions a Qt version (see `DECISIONS.md` ADR-009 for the SDK/runtime). No Qt5 compatibility shims. |
| Build | CMake >= 3.21, `qt_standard_project_setup()`, `qt_add_executable`. No qmake. |
| Compiler | GCC >= 12 / Clang >= 15 / MSVC 2022. Warnings on: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor -Wold-style-cast`. Warnings are errors in CI (`-Werror` behind the `OMNIDICT_WERROR` option). |
| Formatting | `clang-format` with the repo `.clang-format`. Formatting is not reviewed by humans; CI runs `clang-format --dry-run --Werror`. |
| Static analysis | `clang-tidy` with the repo `.clang-tidy` (readability, modernize, bugprone, performance, cppcoreguidelines subset). Runs in CI on changed files. |
| Tests | Qt Test (`QTest`) via CTest. Every non-UI module ships a test. |

---

## 2. Project layout

Omnidict is a monorepo with two codebases and one shared test directory:

```
omnidict/
  app/          Qt 6.11 / C++20 client. CMake root is app/, not the repo root (ADR-010).
    src/
      app/        entry point, CLI, identity (version.h.in)
      core/       pure logic, no QtWidgets: bundle, normalize, sqlite_db, changelog, entry_renderer, user_data
      services/   bundle_manager, catalog, downloader, search_engine (from M2/M3)
      platform/   OS-specific backends behind core interfaces (from M4)
      models/     Qt item models for the UI (from M2)
      ui/         QtWidgets: main_window, search_page, entry_page, dialogs (from M2)
      modules/AccountAndLicense/   reserved licensing module path (from M3), see LICENSING.md
      web/        M6 web app support, if any C++ is shared
    tests/        Qt Test, one tst_<module>.cpp per core module
  pipeline/     Python 3.12 package `omnipipe`, managed with uv
    omnipipe/
    tests/
  tests/        shared between pipeline and app: normalize_vectors.json, fixtures/, golden/
  scripts/      dev-build.sh, dev-run.sh, snap-runtime-env.sh
  .github/workflows/
```

Dependency direction inside `app/` is strict and one-way:

```
ui  --> models --> services --> core
ui  --> core
platform --> core
app --> everything
```

* `core/` must compile with only `Qt6::Core` (+ `Qt6::Gui` for `QColor`/`QIcon` if unavoidable). No Qt widgets, no network, no SQLite driver other than the `core/sqlite_db` wrapper it defines.
* `services/` may use `Qt6::Network` and `core/`.
* `models/` and `ui/` may use everything below them. Nothing depends on `ui/`.
* `platform/` provides interfaces in `core/` and implementations under `platform/<os>/`, selected in CMake, not with `#ifdef` soup inside classes.

Every directory is its own `add_library(omnidict_<name> STATIC)` target with explicit
`target_link_libraries`. This makes the dependency arrows enforceable by the linker.
`services/`, `platform/`, `models/`, `ui/`, `packaging/` and `web/` are created in the
milestone that needs them, not before: no dead code.

---

## 3. Naming

| Thing | Convention | Example |
|---|---|---|
| Namespace | `omnidict::<layer>` | `omnidict::core`, `omnidict::ui` |
| Class / struct / enum | `PascalCase` | `Bundle`, `SearchEngine` |
| Method / free function | `camelCase` | `setZoomFactor()` |
| Member variable | `m_camelCase` | `m_bundle` |
| Static member | `s_camelCase` | `s_instance` (should be rare) |
| Constant / constexpr | `kPascalCase` | `constexpr int kSchemaVersion = 1;` |
| Enum values | `PascalCase` in `enum class` | `Theme::Dark` |
| Signals | past tense or noun-ish, no `on` prefix | `bundleInstalled(QString)` |
| Slots / handlers | verb phrase (no `.ui` auto-connect, so no `on<Source><Event>` convention needed) | `applyTheme()` |
| Files | `snake_case.cpp/.h` matching the primary class: `bundle.h` -> `class Bundle`, `main_window.cpp` -> `class MainWindow` | |
| Settings keys | `section/camelCase` | `"window/zoomFactor"` |
| Qt properties | `camelCase` | |
| CMake option/env prefix | `OMNIDICT_` | `OMNIDICT_WERROR`, `OMNIDICT_BUILD_TESTS` |
| CMake target prefix | `omnidict_` | `omnidict_core` |
| Logging category root | `omnidict.` | `omnidict.core`, `omnidict.downloads` |

One class per file pair. Header guards: `#pragma once`.

---

## 4. Classes & ownership

1. **Qt parent-child ownership** for every `QObject`. `new Foo(this)` is fine; `new Foo()` with a
   later `setParent` is not. A `QObject` without a parent must be held in `std::unique_ptr`
   or `QScopedPointer`, never a bare pointer.
2. Non-`QObject` types: value semantics or `std::unique_ptr`. `std::shared_ptr` requires a
   justification comment.
3. Raw pointers are **non-owning observers only**. Never `delete` a raw pointer outside a
   destructor of the owner.
4. Rule of zero. If you write a destructor, you must explain why.
5. No singletons except a top-level `Application` (via `qApp` cast helper) and a `Settings`
   facade (one instance, owned by `Application`, passed by reference to whoever needs it).
   Nothing else may be reached through a global. **Pass dependencies through constructors.**
6. `Q_OBJECT` on every `QObject` subclass. `Q_DISABLE_COPY_MOVE` on every `QObject` subclass.
7. Prefer composition over inheritance. Do not subclass `QMainWindow` to hold app logic: the
   window owns controllers, it isn't one.
8. Hard size limits: a class > 500 lines or a function > 60 lines is a review blocker. Split it.
9. No `friend`. No `protected` data members.
10. Interfaces (`class INotifier { public: virtual ~INotifier() = default; ... }`) for anything
    with more than one implementation (platform backends, test doubles).

---

## 5. Qt idioms

* **Signals/slots**: always the pointer-to-member-function syntax. `SIGNAL()`/`SLOT()` string
  macros are forbidden. Lambdas must capture `this` only when a context object is given:
  `connect(obj, &Obj::sig, this, [this]{ ... });`, never a context-less lambda on a long-lived
  sender.
* **No `.ui` files.** Build widgets in code. Reasons: diffable, testable, no `ui_*.h` coupling,
  no generated-code mystery. Layouts belong in a private `setupUi()` method that is only layout,
  no logic.
* **Strings**: `QStringLiteral("...")` for literals, `u"..."_s` (`using namespace Qt::StringLiterals`)
  is acceptable and preferred in new code. `tr()` for anything the user sees. No `QString::fromLatin1`
  for constants.
* **Containers**: `QList`/`QHash` when the value crosses a Qt API boundary; `std::` otherwise.
  Iterate with range-for; `qAsConst`/`std::as_const` when iterating a Qt container that would detach.
* **Enums** used in settings or signals are `enum class` registered with `Q_ENUM`/`Q_ENUM_NS`.
* **Timers**: `QTimer::singleShot` with a context object. No busy waiting, no `processEvents()`.
* **Threads**: none unless measured. The one approved exception in this project: a dedicated
  SQLite worker thread per bundle-manager and a `QThreadPool` for downloads and decompression
  (PLAN.md section 7.4) are pre-approved (ADR-004); nothing else spins up a thread without a
  measurement justifying it.
* **Logging**: `Q_LOGGING_CATEGORY` per module (`omnidict.core`, `omnidict.downloads`, ...).
  `qDebug()` without a category is forbidden. No `std::cout`/`printf`.
* **Deprecated API** is a build error (`QT_DISABLE_DEPRECATED_UP_TO=0x060b00`).
* `QT_NO_CAST_FROM_ASCII`, `QT_NO_CAST_TO_ASCII`, `QT_NO_KEYWORDS` are defined globally
  (use `Q_SIGNALS`/`Q_SLOTS`/`Q_EMIT`).
* Use `QStandardPaths` for every path. Never hardcode `~/.config`, `/tmp`, `AppData`.
* Use `QSettings` **only** through the `core::Settings` facade (typed getters/setters with
  defaults in one place). Direct `QSettings` construction outside `core/settings/` is forbidden.
* SQLite is accessed **only** through `core/sqlite_db` (ADR-003). No QtSql, no raw `sqlite3_*`
  calls anywhere else in the tree.

---

## 6. Error handling

* No exceptions across Qt boundaries. Internal code may use exceptions for programmer
  errors only; never for control flow. Qt is compiled without exception guarantees anyway.
* Fallible operations return `std::optional<T>` or a small `Result<T>` (`core/result.h`),
  never a bool plus out-parameter.
* Every failure that the user could care about is logged with a category and surfaced through
  a signal (`errorOccurred(QString userMessage)`), never a `QMessageBox` from inside `core/` or
  `services/`. Only `ui/` shows dialogs.
* `Q_ASSERT` for invariants, never for input validation.

---

## 7. Settings

* One `core::Settings` class, typed accessors, defaults in one table:
  ```cpp
  [[nodiscard]] Theme theme() const;
  void setTheme(Theme);
  Q_SIGNAL void themeChanged(Theme);
  ```
* Keys are `constexpr` in `settings_keys.h`; never spelled twice.
* Settings emit a signal on change; UI reacts to the signal. Nobody polls.
* No legacy migration to write: Omnidict is greenfield, a fresh profile is always acceptable pre-1.0.

---

## 8. Testing

* `core/` is pure logic and must have high line coverage (checked in CI, not enforced as a
  hard gate for now).
* `ui/` gets a smoke test: construct `MainWindow` offscreen (`QT_QPA_PLATFORM=offscreen`),
  show, process events, destroy: no crash, no leak warnings.
* Tests use only public API. If something is untestable through public API, the design is wrong.
* Test file naming: `app/tests/tst_<module>.cpp`, one `QObject` class per file, `QTEST_MAIN` or
  `QTEST_GUILESS_MAIN`.

---

## 9. Git & review

* Commit style: `area: plain sentence` or `area, area: plain sentence`, for example
  `core: Bundle opens a dictionary and looks up headwords` or
  `pipeline: html_subset sanitizer with tests`. No conventional-commit prefixes, no trailers.
* One logical change per commit. A commit must build and pass tests on its own.
* Every feature change updates `DOCS/PROGRESS.md` and, if it adds a user-visible option,
  `DOCS/FEATURES.md`.
* No commented-out code. No `TODO` without an issue number: `// TODO(#42): ...`.
* No dead code "for later". Git remembers.

---

## 10. Things explicitly banned

| Banned | Why | Do instead |
|---|---|---|
| `MainWindow` doing SQLite access, downloads, settings and entry rendering | god-class, untestable | separate controllers/services owned by the window |
| Multiple `.cpp` files for one class (`main_window_tray.cpp`, ...) | hides that the class is too big | split the class |
| Direct `QSettings` everywhere with retyped keys | typos, no defaults, no signals | `core::Settings` |
| `#ifdef Q_OS_*` blocks inside business logic | unreadable, untestable | platform backends behind interfaces, chosen in CMake |
| `QMessageBox` / dialogs from non-UI code | couples logic to widgets | signals + UI handles them |
| `SIGNAL()`/`SLOT()` macros, `.ui` files, auto-connect slots | not type-checked, hidden coupling | PMF connects, code-built widgets |
| Global mutable state (`static QString g_...`), `extern` variables | untraceable | constructor injection |
| Third-party code copied into `src/` without a manifest | unmaintained forks | `FetchContent`/submodule with version pinned + `THIRD_PARTY.md` |
| Feature flags via magic settings keys | undiscoverable | explicit option in settings facade + UI |
| Blocking network / disk on the GUI thread (`QEventLoop` spin) | freezes | the approved worker thread / thread pool (ADR-004), signals back to the UI |
| QtSql or raw `sqlite3_*` calls outside `core/sqlite_db` | bypasses the RAII wrapper and the connection-per-thread rule | `core::SqliteDb` only (ADR-003) |
| Bare `except:` in pipeline code | swallows real errors | catch the specific typed exception |

---

## 11. Python (pipeline)

* Python 3.12. Full type hints everywhere; `mypy --strict` clean.
* `ruff check` and `ruff format` clean, line length 100.
* Tests with `pytest`, under `pipeline/tests/test_<module>.py`; test names describe the
  behaviour under test, not the implementation.
* `pathlib.Path` for every filesystem path; never bare strings.
* The `logging` module with a module-level logger (`logger = logging.getLogger(__name__)`);
  no `print` in library code. CLI entry points may write results to stdout.
* Records are frozen dataclasses (`@dataclass(frozen=True)`).
* No network access in tests.
* No bare `except`. Errors are typed exceptions defined in the module that raises them
  (for example `class HtmlSubsetError(ValueError)` in `html_subset.py`).
* Naming: snake_case modules and functions, PascalCase classes, UPPER_CASE constants.
* Every converter is independently runnable with `python -m omnipipe.converters.<name>`.

---

## 12. Shared vectors and golden files

* `tests/normalize_vectors.json` is read by both the Python and the C++ normalization tests
  and is the contract between them (ADR-005, ADR-006). Any change to normalization changes the
  vectors in the same commit.
* `tests/fixtures/` holds hand-written inputs shared by both sides (for example
  `sample-en.jsonl` and `sample-en.meta.json`, used to build the fixture bundle both `make
  fixture` and the C++ tests open).
* Golden files (expected rendered HTML for entries, from M2) live under `tests/golden/` and are
  compared byte for byte. Golden files are regenerated only deliberately and the diff is
  reviewed like any other change, never regenerated to make a failing test pass without reading
  why it failed.

---

## 13. Shell

* `#!/usr/bin/env bash` and `set -euo pipefail` at the top of every script.
* `shellcheck` clean.

---

## 14. Definition of Done (per feature)

- [ ] Code follows every section above; `clang-format` and `clang-tidy` clean; `ruff`/`mypy`/`pytest` clean for pipeline changes.
- [ ] Unit tests for all `core/` logic touched; smoke test still passes.
- [ ] Works on Linux (X11 + Wayland); Windows/macOS-specific code behind platform backends.
- [ ] Logging category added/used; no silent failures.
- [ ] `DOCS/FEATURES.md` row updated (status, implementing class).
- [ ] `DOCS/PROGRESS.md` entry.
- [ ] No new third-party dependency without an ADR in `DOCS/DECISIONS.md` and a row in `THIRD_PARTY.md`.
