# FEATURES: the scope contract

Decision legend: **KEEP** (planned for the milestone shown), **LATER** (v2 or beyond), **DROP** (never).
Status legend: `done`, `in progress`, `planned` (not started), `-` (not applicable, LATER/DROP rows).

Source: `DOCS/PLAN.md` sections 5.3, 6, 7.2, 7A and 8, plus the app-shell rows the rewrite kit
expects every ktechpit desktop app to have.

## A. Pipeline

| # | Feature | Milestone | Decision | Status |
|---|---|---|---|---|
| A1 | Canonical JSONL schema and dataclasses (`schema.py`) | M0 | KEEP | done |
| A2 | Restricted HTML subset validator (`html_subset.py`) | M0 | KEEP | done |
| A3 | Headword normalization (`normalize.py`, ICU) with shared test vectors | M0 | KEEP | done |
| A4 | `build.py`: JSONL to `dict.sqlite` | M0 | KEEP | done |
| A5 | Pipeline CI quality gates: `html_subset` zero-strip, non-empty preview, round-trip Bundle lookup, schema-migration test | M0 | KEEP | done (CI workflow written, not yet run on GitHub; the schema-migration test arrives with the first `schema_version` bump) |
| A6 | `package.py`: sqlite to `.odict` (zstd) plus `manifest.json` and sha256 | M1 | KEEP | done |
| A7 | `catalog.py`: regenerate `catalog.json` from all manifests | M1 | KEEP | done |
| A8 | Kaikki converter (Wiktionary via kaikki.org) | M1 | KEEP | done |
| A9 | FreeDict converter | M4 | KEEP | done: `converters/freedict.py`, 291 of 305 dictionaries eligible (free licence read from each TEI header, at least 1000 headwords), all built and packaged locally |
| A10 | WordNet converter | M4 | KEEP | done: `converters/oewn.py` (Open English WordNet 2025, CC BY 4.0), `oewn-en` 128,009 entries |
| A11 | CC-CEDICT converter | M4 | KEEP | done: `converters/cedict.py`, `cedict-zh-en` 121,362 entries, pinyin as forms |
| A12 | JMdict converter | M4 | KEEP | done: `converters/jmdict.py`, `jmdict-ja-en` 218,840 entries, readings and romaji as forms |
| A13 | KEngDic converter | M4 | KEEP | planned |
| A14 | StarDict community import, opt-in "unofficial" tier after licence review | v2 | LATER | - |

## B. Bundle and search core

| # | Feature | Milestone | Decision | Status |
|---|---|---|---|---|
| B1 | `core::Bundle` opens one `dict.sqlite`, exposes lookup/search | M0 | KEEP | done |
| B2 | `core::SqliteDb` RAII wrapper over the sqlite3 C API (ADR-003) | M0 | KEEP | done |
| B3 | `core::normalize` mirrors the pipeline normalization, shared test vectors | M0 | KEEP | done |
| B4 | As-you-type prefix search across open bundles (`headword_norm`, `forms.form_norm`), debounced, merged and grouped into `ResultsModel` | M2 | KEEP | done (`core::SearchEngine`, 80 ms debounce in `MainWindow`) |
| B5 | Empty query shows history | M2 | KEEP | done (favourites and recent entries) |
| B6 | Exact match, then prefix, then FTS full-text ("Also found in definitions") | M2 | KEEP | done; dictionaries ranked by match quality, headword matches before form matches |
| B7 | Wildcard search (`?`, `*`) | M2 | KEEP | done (`Bundle::searchPattern`) |
| B8 | Spell suggestion on zero results | M2 | KEEP (owner, 2026-09-27: our own suggester, ADR-013) | core done: `suggest` table (schema 2), Python reference and `core::Suggester` agree on `tests/suggest_cases.json`; worst case 19 ms on English (`tst_real_bundles`); UI done (mocks/main-no-results.html: "Did you mean" in the list, a no-match state beside it) |
| B9 | "All" dictionary filter dropdown restricts fan-out to one bundle | M2 | KEEP | done |
| B10 | `services::BundleManager`: installed bundles, open/close, LRU of open connections | M2 | KEEP, lazy opening deferred (measured 2026-09-28) | done as `core::Library`: opens every dictionary at start. Measured: 54 dictionaries, English among them, start in 0.87 s to a shown sheet with 76 MB peak (4 dictionaries: 0.85 s, 64 MB), inside PLAN 7.3's targets, so the LRU waits until a measurement says otherwise |
| B11 | `services::SearchEngine`: fan-out query across bundles, merge results | M2 | KEEP | done as `core::SearchEngine` (pure, on the lookup thread) |
| B12 | Dedicated SQLite worker thread per bundle-manager (ADR-004) | M2 | KEEP | done: one lookup thread (`services::LookupService`) |
| B13 | DAWG / perfect-hash search index | v2 | LATER | - |
| B14 | MeCab/Jieba tokenization for CJK lookup | v2 | LATER | - |

## C. Entry view

| # | Feature | Milestone | Decision | Status |
|---|---|---|---|---|
| C1 | `core::EntryRenderer`: entry to HTML, client-agnostic template (PLAN D12) | M2 | KEEP | done (`core::renderEntry`, contract in DOCS/entry-html.md) |
| C2 | Visual structure: headword, frequency, IPA, POS heading per sense, pattern, definition, label box, examples | M2 | KEEP | done; the label box is a shaded background, since QTextBrowser draws no borders on inline text |
| C3 | `lex:` links resolve to an in-app lookup; external links disabled | M2 | KEEP | done |
| C4 | Star button adds a favorite; every opened entry is added to history (`core::UserData`) | M2 | KEEP | done (`core::UserData`); history records entries the user opens, not the as-you-type preview |
| C5 | Golden-file tests for rendered entry HTML (`tests/golden/`) | M2 | KEEP | done: 9 goldens shared by the C++ and Python renderers |

## D. Manage dictionaries

| # | Feature | Milestone | Decision | Status |
|---|---|---|---|---|
| D1 | Manage dictionaries dialog: My dictionaries / Available tabs, source/target filters | M3 | KEEP | done (`ui::DictionariesDialog`, mocks/dictionaries*.html: order by dragging or Alt+Up/Down, switch off, update, remove; From/To language filters) |
| D2 | Row: name, publisher, size, action button (download / progress ring / delete / update) | M3 | KEEP | done |
| D3 | Multiple concurrent downloads allowed (max 2) | M3 | KEEP | done (backend): `services::DictionaryManager::kMaxConcurrentDownloads`, the rest queue and start as a slot frees; UI still to wire up |
| D4 | `core::Catalog`/`core::parseCatalog`: parses `catalog.json` strictly, skips entries with a newer `schema_version`; `services::DictionaryManager` fetches it (24h cache, `OMNIDICT_CATALOG_URL`) and exposes it for the UI to diff against installed | M3 | KEEP | done (backend); UI diffing/tabs still planned |
| D5 | `core::installBundle`/`core::removeInstalled`: sha256 + size verify (streamed), zstd decompress, `Bundle::open` re-check, install and remove stale versions; `services::DictionaryManager` drives the resumable download (`Range` header, falls back to a fresh download if ignored) and calls it | M3 | KEEP | done (backend); UI progress/actions still planned |
| D6 | Downloads on the GUI thread's `QNetworkAccessManager` (non-blocking, no thread needed for async I/O); the verify+decompress step (`core::installBundle`) runs on `QThreadPool::globalInstance()` (ADR-004), result posted back via `QMetaObject::invokeMethod` | M3 | KEEP | done (backend) |
| D7 | Renders the `attribution` line for every installed bundle plus the app's own open-source notices | M3 | KEEP | done: About's Dictionaries tab credits each dictionary with its licence link; the Open source tab lists Qt, Qt Svg, SQLite, ICU, zstd and Lucide; every entry ends with its dictionary's credit |

## E. App shell

| # | Feature | Milestone | Decision | Status |
|---|---|---|---|---|
| E1 | Settings facade (`core::Settings`), typed accessors, one defaults table, change signals | M2 | KEEP | done (`core::Settings`, INI in the profile directory, `tst_settings`) |
| E2 | Rotating log sink with diagnostics | M2 | KEEP | done (`core::LogSink` from the kit: `<profile>/logs/omnidict.log`, 2 MB rotation, last 1000 lines in memory; `tst_log_sink`) |
| E3 | Single instance | M2 | KEEP | done (`app::SingleInstance` from the kit, one per profile directory; a second launch hands its word to the open window; `tst_single_instance`) |
| E4 | Theme: system / light / dark | M2 | KEEP | done: `ui::ThemeApplier` follows the setting and the desktop live; tokens in `ui::Tokens` match DOCS/mocks/mock.css |
| E5 | About dialog with diagnostics | M2 | KEEP | done (mocks/about.html: dictionary credits, open-source notices, diagnostics tab; `tst_sheets`) |
| E6 | Report a bug | M2 | KEEP | done (mocks/bug-report.html: GitHub issue or email, diagnostics on the clipboard, nothing sent by the app) |
| E7 | Keyboard shortcuts sheet (F1 and Ctrl+/) | M2 | KEEP | done (mocks/shortcuts.html, filterable) |
| E8 | What's new sheet from the bundled changelog | M2 | KEEP | done (mocks/whats-new.html; once after an update, and from the menu and About) |
| E9 | Account and Pro plan module (`AccountAndLicense`) | M3 | KEEP | planned |

## F. Packaging

| # | Feature | Milestone | Decision | Status |
|---|---|---|---|---|
| F1 | Snap packaging, built in CI | M5 | KEEP | planned |
| F2 | Flatpak packaging | M5 | KEEP | planned |
| F3 | AppImage packaging | M5 | KEEP | planned |
| F4 | CI release workflow | M5 | KEEP | planned |
| F5 | Windows / macOS builds | v2 | LATER | - |

## G. Web

| # | Feature | Milestone | Decision | Status |
|---|---|---|---|---|
| G1 | Static entry-page pre-generation from bundles; no second data path | M6 | KEEP | planned |
| G2 | Dynamic server: search-as-you-type API, "did you mean", catalog pages, fallback render | M6 | KEEP | planned |
| G3 | Shared rendering template with the desktop `EntryRenderer`; golden-file parity test | M6 | KEEP | planned |
| G4 | SEO/compliance: attribution footer, `sitemap.xml`, canonical/hreflang, JSON-LD, `noindex` on search/API, ad slot reservation | M6 | KEEP | planned |
| G5 | Indian-language focus with Latin-script transliteration search | M6 | KEEP | planned |
| G6 | Cross-dictionary single-page results | M6 | KEEP | planned |
| G7 | App funnel: web links to the desktop app and back | M6 | KEEP | planned |

## Non-goals (v1)

| # | Feature | Milestone | Decision | Status |
|---|---|---|---|---|
| N1 | Mobile builds | - | DROP | - |
| N2 | User-contributed / edited dictionaries | - | DROP | - |
| N3 | Licensed commercial content (Collins, Oxford, etc.); schema must not block it for a later deal | - | DROP | - |
| N4 | OCR / camera lookup | - | DROP | - |
| N5 | Cloud sync of user data | - | DROP | - |
