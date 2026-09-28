# Progress

Newest first. One entry per working session.

## Milestone status

| Milestone | Status |
|---|---|
| M0 Scaffold | done locally; CI not yet run (no remote) |
| M1 Kaikki + packaging + catalog | done: three real bundles, catalog served and verified locally |
| M2 Qt client (search, entry view, app shell) | done: every approved M2 mock built; lazy opening measured and not needed yet (B10) |
| M3 Manage dictionaries + downloader + About | download/install backend done; dialog UI still to do |
| M4 More converters, real catalog | done locally: every converter written (KEngDic added 2026-09-28); 328 Wiktionary languages listed, built in full by CI once the repository exists; hosting on GitHub Releases decided (ADR-016), publishing waits for the owner |
| M5 Packaging | done locally: desktop file, metainfo, snap, Flatpak manifest, snap and release workflows, store screenshots (ADR-017); AppImage deferred; nothing runs until the repository exists |
| M6 Web app | todo |

## 2026-09-28 - M5 packaging

- Owner: AppImage deferred, snap name `omnidict`, links on github.com/keshavbhatt/omnidict.
- `app/dist/linux/`: desktop file (validates) and metainfo (validates; `<release>` comes with
  the release checklist), installed by CMake with the binary and icons.
- Store text tuned with the FlathubSEO playbook at the owner's request: summary "Offline
  dictionary for any language" (35), first keywords dictionary, wiktionary, thesaurus,
  "dictionary" in ten languages, languages named in the description. A Flathub search baseline
  showed the "<language> dictionary" queries have no real answer today
  (FlathubSEO/docs/omnidict-keyword-research.md).
- `snap/snapcraft.yaml` (kde-neon-6, expands cleanly), `.github/workflows/snap.yml`,
  `.github/workflows/release.yml` (tag checks tested locally), Flatpak manifest (passes
  flatpak-builder-lint; the runtime's SQLite has FTS5 and trigram).
- `scripts/store-screenshots.sh`: four plain captures in `screenshots/store/`.
- A download test through an HTTP redirect (catalogue, download, resume with Range), the way
  GitHub serves release assets.
- CHANGELOG 0.1.0 rewritten for the catalogue app (it still described three built-in
  dictionaries).

## 2026-09-28 - Hosting, Wiktionary languages, Kengdic

- Hosting (owner): GitHub Releases, bundles built and attached by GitHub Actions, upstream
  sources not re-hosted (ADR-016, PLAN 9 question 2). Nothing is published yet.
- Wiktionary (owner): every Kaikki language with at least 1,000 senses, historical languages
  in, Translingual out. `kaikki.py --discover` writes the checked-in
  `pipeline/kaikki-languages.tsv` (320 languages; the 8 reconstructed `Proto-` languages wait
  for the owner). Dump URLs now drop spaces and punctuation from the file name, which 16
  languages need. A sample of 12 new languages (Old English, Ancient Greek, Gothic, Sumerian,
  Egyptian, Korean, Cantonese, Middle English, Old Norse, Navajo, Hokkien, Latin) was built
  locally; the full set is for CI, since this machine's disk cannot hold it. Cantonese and
  Hokkien came out nearly empty, so every language was converted in a streamed dry run: only
  the three Chinese varieties (Mandarin, Cantonese, Hokkien, filed under "Chinese" by
  Wiktionary) are empty, and they are now excluded.
- Kengdic converter (FEATURES A13): `kengdic-ko-en`, 106,211 entries, hanja as forms, graded
  senses first; licence `MPL-2.0 OR LGPL-2.0-or-later` from the upstream README
  (DOCS/sources.md "Kengdic notes" has the discrepancy with its `datapackage.json`). Added to
  `tests/known_headwords.json`.
- Owner-reported fixes: language drop-downs open a 12-row scrolling list instead of a
  screen-high popup (`combobox-popup: 0`); "English", "español" and "中文" replace Qt's
  territory names ("American English", "español de España", "简体中文"); the About footer
  spans the sheet like the other sheets.
- Mocks approved and built (owner, 2026-09-28): the Available tab counts the catalogue
  ("Available (308)", "Showing 24 of 308 dictionaries."), filters by provider (from the
  catalogue's `source.converter`, now parsed by `core::Catalog`; `core::providerName` maps it
  to Wiktionary, FreeDict, ...), and shows entries, size and action in aligned columns; an
  empty Installed tab shows "No dictionaries yet" with Browse dictionaries, which opens the
  Available tab. Verified headlessly against the local 308-dictionary catalogue.
- Owner-reported, fixed: the removal confirmation's footer spans the sheet; the mouse's back
  and forward buttons go Back and Forward anywhere in the main window (on release, as in a
  browser, only while the toolbar button is enabled).
- Check for updates (mock approved 2026-09-28): "Checking..." while the catalogue is read,
  then "N updates available", "All dictionaries are up to date" or "Could not check: ...".
- Owner decisions, same day, built: schema 3 adds optional `source_lang_name`,
  `target_lang_name` and `source_url` to bundles and manifests (every converter fills them;
  schema 2 bundles still open). The language filters use the English name where Qt has none
  (about half the Wiktionary codes). The 8 reconstructed `Proto-` languages are in (328
  Wiktionary languages, 623 dictionaries in all). The rolling release is written:
  `package.py --flat-urls`, `catalog.py --previous --manifests-only`, `make ci-bundle-<id>`
  and `.github/workflows/dictionaries.yml` (manual start only), checked locally end to end on
  Sumerian; it cannot run until the repository exists.
- About cards link each dictionary's upstream source after its licence, named by its site
  (mock approved 2026-09-28); Kengdic links GitHub's page for the file at the pinned commit.

## 2026-09-27 - M3 download and install backend

Backend half of "Manage dictionaries" (PLAN 5.3, 7.2, 7.4), matching `pipeline/omnipipe/package.py`
and `catalog.py` exactly; the dialog UI is separate work.

- `core::Catalog`/`core::CatalogEntry`/`core::parseCatalog`: strict `catalog.json` parsing (a
  missing or wrong-typed required field names the offending entry; an entry with a newer
  `schema_version` than `Bundle::kSupportedSchemaVersion` is skipped, not an error);
  `core::compareVersions` for dotted numeric versions.
- `core::installBundle`/`core::removeInstalled`/`core::installedSize`: verifies an `.odict`'s
  size and streamed sha256 against its catalogue entry, zstd-decompresses it (streaming
  `ZSTD_decompressStream`) into `<root>/<dictId>/<version>/dict.sqlite.part`, renames it into
  place only after `Bundle::open` confirms its `dict_id` and `version` match, then removes every
  other installed version of that dictionary. Any failure removes the partial output and leaves
  a previously installed version untouched.
- `services::DictionaryManager`: GUI-thread `QObject` owning a `QNetworkAccessManager`. Fetches
  and caches `catalog.json` (24h TTL unless forced, `OMNIDICT_CATALOG_URL` overrides the default
  `http://localhost:8000/catalog.json`). Queues downloads, at most 2 concurrent (PLAN 7.2), with
  resume through a `Range` header (falls back to a fresh download when a server ignores it); the
  verify+decompress step runs on `QThreadPool::globalInstance()` (ADR-004), result posted back
  with `QMetaObject::invokeMethod`. `cancel()` keeps the partial file for a later resume;
  `remove()` uninstalls.
- ADR-015 (Qt Network + zstd); `THIRD_PARTY.md` zstd row updated from "from M3" to in-use, new
  Qt Network row; `app/cmake/FindZstd.cmake` (CMake ships no module of its own); dev build links
  zstd through the core24 runtime farm the same way SQLite already does.
- Tests: `tst_catalog`, `tst_installer` (builds its own `.odict` with the zstd C API), and
  `tst_dictionary_manager` (a `QTcpServer`-based HTTP server, 127.0.0.1 only: catalogue fetch,
  the 2-at-a-time limit, cancel, a 404 failure message, and a full install through a resumed
  Range request). `make test` and `make lint` both clean.

## 2026-09-28 - M4: four new sources, 297 dictionaries

- Converters, written in parallel by four agents in worktrees and merged here: Open English
  WordNet (`oewn-en`, 128,009 entries), JMdict (`jmdict-ja-en`, 218,840; readings and romaji
  as forms, so `taberu` finds 食べる), CC-CEDICT (`cedict-zh-en`, 121,362; pinyin with tone
  marks as forms, so `nihao` finds 你好) and FreeDict (291 of 305 dictionaries: a free
  licence read from each TEI header and at least 1000 headwords). Licences and notes for each
  in `DOCS/sources.md`, the FreeDict table generated by `--list --markdown`.
- Checked by hand: licence headers of a sample of FreeDict dictionaries against what the
  converter decided. Fixed from that: a header naming several licences gives an SPDX `AND`
  (deu-eng: GPL-3.0-only AND AGPL-3.0-only), an explicit capital "OR under the terms" gives
  `OR` (eng-ell), and within one family only the highest version counts. Also fixed from the
  full build: the dictionary's own TEI file is read (not a header-only one), XInclude is
  followed (eng-pol), translations marked on `<quote>` and nested senses are read (224
  dictionaries nest senses; all were rebuilt).
- Copyleft: GPL dictionaries oblige the publisher to link their source (the FreeDict release
  and this converter); noted in `DOCS/sources.md` for when publishing is decided.
- App: sans-serif fallbacks for Chinese, Japanese and Korean text (the desktop picked a serif
  face); credits no longer end in a doubled full stop. The Available tab lists all 297 and
  opens in about 2 s from start-up.
- Tests: 355 pipeline, 17 C++ suites; the real-dictionary gate now covers 8 bundles, with
  spelling cases in Chinese, Japanese, German and French (all answer within 25 ms).
- Local build: `pipeline/out` 4.6 GB, sources 878 MB, 16 GB free.

## 2026-09-27 - Mocks approved; schema 2 with spelling suggestions; M2 shell and M3 built

Owner decisions: the 14 mocks approved; our own spell suggester (ADR-013); schema_version 2
(ADR-012 indexes plus the `suggest` table).

- Schema 2: `suggest` FTS5 trigram table with start and end markers; Python reference
  (`omnipipe.suggest`) and `core::Suggester` agree on `tests/suggest_cases.json`; worst case
  19 ms on English, 4 ms Spanish, 1 ms Hindi (`tst_real_bundles`). Bundles rebuilt (English
  809 to 844 MB) and repackaged; the local catalogue lists schema 2.
- UI from the mocks: design tokens with a live theme switch, tinted Lucide glyphs (ADR-014),
  settings facade, header with search field and dictionary filter, grouped results with
  stars, "Did you mean", collapsible "Also found in definitions", entry bar with Back,
  Forward, Copy and the star, credit line under every entry, empty and no-match states, one
  column below 720 px. Sheets: settings, shortcuts, What's new, report a bug, About with
  credits, open-source notices and diagnostics. Clearing history can be undone.
- App shell from the kit: log sink, single instance per profile.
- M3: catalogue parsing, verified install (size, sha256, zstd, re-open check), download
  manager with 2 at a time and resume (ADR-015, Qt Network and zstd), first-run screen,
  Dictionaries sheet (order, switch off, update, remove, language filters). Headless check:
  `OMNIDICT_DEBUG_INSTALL=wikt-hi-en` installed the real package in 0.7 s, English (262 MB) in
  6 s, and the window searched them.
- Work split across three agents in worktrees (sheets, backend, Dictionaries sheet), merged
  and reviewed here; every screen checked against its mock in screenshots.
- Deviations from the mocks, since closed (2026-09-28): rows found through an inflected form
  now show that form with "from <headword>" (`EntryPreview::matchedForm`); the language filters
  are drawn as the mock's chips; the Installing ring turns. Owner, 2026-09-28: no dictionary
  ships with the app (PLAN 9 question 5). Update checked end to end: a real Hindi package
  marked 2026.09.2 installed over 2026.09.1 and removed the old folder. Start-up with 54
  dictionaries: 0.87 s and 76 MB peak, so lazy opening waits (FEATURES B10). Owner,
  2026-09-28: no telemetry (PLAN 9 question 6); the licensing module and the GitHub remote
  wait until later.
- Tests: 253 pipeline, 17 C++ suites. Lint clean.

## 2026-09-27 - Design mocks, spell-suggestion research, publishing stays local

Owner direction: mocks for everything before more UI work; nothing published until ready;
research spellfix1 against a custom suggester before choosing.

- `DOCS/DESIGN.md` (tokens from the app icon, layout, screen list, rules) and 14 HTML mocks in
  `DOCS/mocks/` following the approach of the owner's Ultimate Media Downloader mocks: the
  current window restyled plus the rest of M2 (empty search, no match, followed link, filter,
  main menu, settings, shortcuts, What's new, bug report, About) and M3 (first run,
  dictionaries installed and available). New in the design: an entry bar with Back, Forward,
  Copy and the star, and a credit line at the end of every entry. All await review.
- Spell suggestion: both methods measured on the three real bundles
  (`DOCS/spell-suggestion.md`). spellfix1 returns unrelated words for every Hindi query; our own
  trigram and edit-distance method matches it on English and Spanish. Recommendation: our own,
  in the bundle, with `schema_version` 2 shared with ADR-012 (ADR-013, proposed). Not
  implemented.
- PLAN 9 question 2 answered: publishing stays local. CLAUDE.md gains the mocks-first and
  local-publishing rules.

## 2026-09-27 - M1 delivered, M2 core features

M1:
- Kaikki converter (`omnipipe.converters.kaikki`): streams gzipped dumps, merges a word's
  records into one entry, skips inflection-only records (their forms stay on the lemma), keeps
  romanizations as forms, maps grammar tags to `pattern` and usage tags to `label`, turns wiki
  links into `lex:` links. Notes and numbers in DOCS/sources.md.
- Bundles from the 2026-09-25 dumps: `wikt-hi-en` 23,029 entries (57 MB, 11.5 MB packed),
  `wikt-es-en` 113,073 (169 MB, 40 MB), `wikt-en` 883,985 (809 MB, 262 MB). English builds in
  5 minutes with flat memory (112 MB); it skips the final VACUUM because the disk is 98% full.
- `package.py` (zstd `.odict`, manifest, sha256; deterministic), `catalog.py`, `make fetch /
  build / package / catalog / serve / all`. Acceptance: all three bundles downloaded from a
  local HTTP server and verified (size, sha256, installed size); `test_serve.py` runs the same
  flow on the fixture in CI.
- Round-trip gate (`tst_real_bundles`): the C++ `Bundle` answers 20 known headwords, 3
  inflected or romanized forms, a prefix and a full-text query in every built bundle.
- It caught two real problems. Typing `día` returned the entry `dia` first: exact spellings
  now rank first. Opening an English entry took over a second because child tables had no
  parent index: four indexes added (ADR-012, for the owner to confirm), 22.6 s to 45 ms for
  the whole gate; `test_client_queries_never_scan_a_table` keeps it so.
- `build.py` streams in batches and builds indexes after the load.

M2 (core features; the kit's app-shell pieces are still open, see FEATURES E1 to E8):
- Entry HTML contract (DOCS/entry-html.md) with two independent renderers, C++
  (`core::renderEntry`) and Python (`omnipipe.render`), agreeing byte for byte on 9 golden files.
- `core::Library` (bundle discovery, both layouts, newest version wins), `core::SearchEngine`
  (grouped results, exact then prefix then "also found in definitions", wildcards, dictionary
  filter, dictionaries ranked by match quality), `core::UserData` (history and favourites).
- Window: search field with 80 ms debounce, dictionary filter, grouped result list, entry view
  with `lex:` links, star button (Ctrl+D), favourites and recent entries on an empty search.
  All dictionary access on one lookup thread (`services::LookupService`, ADR-004).
- Run it: `scripts/dev-run.sh -- --bundles pipeline/out --profile <scratch dir>`.
  Headless: `OMNIDICT_DEBUG_QUERY=perro OMNIDICT_DEBUG_GRAB=shot.png QT_QPA_PLATFORM=offscreen`.
- Verified: 8 C++ suites (including `tst_smoke`, which drives the window offscreen through
  search, open, star and history), 230 pipeline tests, lint clean; screenshots checked for
  Spanish, English and romanized Hindi queries.
- Not yet: kit app-shell pieces (settings, log sink, single instance, theme switch, About,
  bug report, shortcuts sheet, What's new), spell suggestion (open question), lazy opening
  with an LRU of connections (PLAN 7.3), translations.

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
