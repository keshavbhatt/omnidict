# Omnidict: Project Plan

> Working name: **Omnidict**. This document is the source of truth for architecture and scope.
> Written for use with Claude Code. Sections marked **DECISION** are settled; do not re-litigate without asking the owner.
> Sections marked **OPEN** need the owner's input before implementing.

---

## 1. Goal

A cross-platform (Linux first, then Windows/macOS) desktop dictionary application built in **Qt 6 / C++**, where the user can browse a large catalog of dictionaries and download them on demand. Every dictionary, monolingual or bilingual, any script, is stored and rendered in **one consistent format**.

Reference UX: Samsung's built-in "Dictionary" app (DioDict engine by SELVAS AI / Diotek). We replicate its architecture and UX, **not** its content or file format (both proprietary).

### Non-goals (v1)
- Mobile builds
- User-contributed/edited dictionaries
- Licensed commercial content (Collins, Oxford, etc.), possible later via publisher deals; schema must not block it
- OCR / camera lookup
- Cloud sync of user data

---

## 2. Key decisions (DECISION)

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | Two separate codebases in one monorepo: `pipeline/` (Python, runs on server) and `app/` (Qt/C++, runs on desktop) | Data conversion is batch work; the client never parses source formats |
| D2 | One canonical dictionary bundle format: a single **SQLite** file per dictionary, zstd-compressed for transport | Simple, mmap-friendly, FTS5 built in, no custom index code in v1 |
| D3 | All source dictionaries are normalized into the canonical schema (section 4) by per-source converters | This is the "consistency" requirement |
| D4 | Content sources are open/free only for v1: Wiktionary (via Kaikki), FreeDict, WordNet, CC-CEDICT, JMdict, KEngDic | Legally redistributable; Wiktionary alone yields hundreds of language pairs |
| D5 | Definition text uses a **restricted HTML subset** (see section 4.3); converters reject/strip everything else | Rich enough for bold/italic/cross-refs, small enough to render safely in QTextBrowser |
| D6 | User data (history, favorites, settings) lives in a separate SQLite DB in the user's app-data dir; never inside dictionary bundles | Bundles are read-only and replaceable |
| D7 | Catalog is a static `catalog.json` on a CDN/object store; no API server needed for v1 | Solo-maintainable; cheap |
| D8 | Search index v1 = SQLite FTS5 + `headword_norm` prefix index. DAWG / perfect-hash index is a **v2 optimization** | Ship first; optimize when schema is stable |
| D9 | Compression: **zstd** (not LZMA) | Faster decompression, permissive licence |
| D10 | Attribution and licence text are mandatory manifest fields and rendered in-app automatically | CC BY-SA compliance; mirrors Samsung's About screen |
| D11 | Every bundle is versioned; client checks catalog for updates | Beat stale licensed data (Samsung ships Collins 2008) by rebuilding from fresh dumps |
| D12 | A **server-side web app** is a planned first-class consumer of the same bundles (last milestone, M6). Bundle format, normalization and entry-rendering logic must stay client-agnostic: no Qt types in `EntryRenderer`'s template logic; shared test vectors for normalization | Web version is monetized via ads and needs SSR for SEO; must reuse pipeline output without a second data path |
| D13 | Kit conventions from `rewrite-kit` (PLAYBOOK.md, MANIFEST.md, CODING_STANDARDS.md) are binding for file naming, `.ui` usage and static-library layering; see `DOCS/CODING_STANDARDS.md` and ADR-001 | Reuse a proven, already-written standard instead of inventing one for a greenfield app |
| D14 | UI toolkit is Qt Widgets, not QML | Matches the kit's pattern; resolves open question 1 |
| D15 | Qt 6.11, built against the `kde-qt6-core24-sdk` snap SDK and run against the `kf6-core24` runtime snap | Dev/prod parity: what compiles against the SDK is what the shipped snap runs against |
| D16 | Licence model: GPL-3.0-or-later for the program, the Ktechpit licensing module under its own licence (the kit's "Model B"), REUSE compliant | Open source with a reserved path for a future account/licensing module, matching the kit's precedent |
| D17 | App identity: id `com.ktechpit.omnidict`, org `ktechpit`, domain `ktechpit.com`, developer Keshav Bhatt | Consistent with every other ktechpit app; resolves part of open question 3 |

---

## 3. Repository layout

```
omnidict/
  CLAUDE.md
  README.md
  CHANGELOG.md
  LICENSING.md
  REUSE.toml
  THIRD_PARTY.md
  Makefile
  .clang-format
  .clang-tidy
  .editorconfig
  .gitignore
  LICENSES/
  DOCS/
    PLAN.md                     # this file
    README.md                   # index of DOCS/, workflow
    CODING_STANDARDS.md         # binding Qt 6 / C++20 (and pipeline) rules
    FEATURES.md                 # scope contract
    DECISIONS.md                # ADRs
    PROGRESS.md                 # session log
    ROADMAP.md                  # milestones with exit criteria
    LESSONS.md                  # rules inherited from the kit's PLAYBOOK
    schema.md                   # canonical schema reference (generated from section 4)
    sources.md                  # per-source notes, licence, dump URLs
  tests/                        # shared between pipeline and app
    normalize_vectors.json      # contract between Python and C++ normalization
    fixtures/
      sample-en.jsonl
      sample-en.meta.json
    golden/                     # M2: expected rendered entry HTML, compared byte for byte
  pipeline/                     # Python 3.12, managed with uv
    pyproject.toml
    uv.lock
    Makefile
    omnipipe/
      __init__.py
      schema.py                 # dataclasses for canonical JSONL records
      html_subset.py            # sanitizer/validator for definition markup
      normalize.py              # headword normalization (ICU via PyICU)
      build.py                  # JSONL -> dict.sqlite
      package.py                # M1: sqlite -> .odict (zstd) + manifest.json + sha256
      catalog.py                # M1: regenerate catalog.json from all manifests
      converters/
        __init__.py
        base.py                 # Converter ABC: fetch(), iter_records()
        kaikki.py                # M1: Wiktionary via kaikki.org JSONL
        freedict.py              # M4: FreeDict TEI XML
        wordnet.py               # M4
        cedict.py                # M4
        jmdict.py                # M4
        kengdic.py               # M4
    tests/
      test_schema.py
      test_html_subset.py
      test_normalize.py
      test_build.py
  app/                          # Qt 6.11 / C++20, CMake
    CMakeLists.txt
    cmake/
      Warnings.cmake
      Version.cmake
      SnapSdkWorkaround.cmake
    src/
      CMakeLists.txt
      app/                      # M0: entry point, CLI
        main.cpp
        lookup_command.h
        lookup_command.cpp
        version.h.in            # identity constants; every other file reads from here
        CMakeLists.txt
      core/                     # M0: pure logic, no QtWidgets
        bundle.h
        bundle.cpp               # opens one dict.sqlite, exposes lookup/search
        normalize.h
        normalize.cpp            # headword normalization, mirrors pipeline/omnipipe/normalize.py
        sqlite_db.h
        sqlite_db.cpp            # RAII wrapper over the sqlite3 C API
        changelog.h
        changelog.cpp            # Keep-a-Changelog section extractor
        entry.h                  # plain data types for an entry
        result.h                 # small Result<T> for fallible operations
        logging.h
        logging.cpp
        CMakeLists.txt
        entry_renderer.h          # M2: entry -> HTML, client-agnostic (D12)
        entry_renderer.cpp
        user_data.h                # M2: history, favorites (SQLite)
        user_data.cpp
      services/                 # M2/M3: orchestration above core
        bundle_manager.h          # M2: installed bundles, open/close
        bundle_manager.cpp
        search_engine.h           # M2: fan-out query across open bundles, merge results
        search_engine.cpp
        catalog.h                 # M3: parses catalog.json, diff vs installed
        catalog.cpp
        downloader.h               # M3: download, resume, verify
        downloader.cpp
      platform/                 # M4: OS-specific backends behind core interfaces
      models/                   # M2: Qt item models for the UI
        results_model.h
        results_model.cpp
        catalog_model.h
        catalog_model.cpp
        history_model.h
        history_model.cpp
      ui/                       # M2/M3: QtWidgets, built in code, no .ui files
        main_window.h             # M2
        main_window.cpp
        search_page.h             # M2
        search_page.cpp
        entry_page.h              # M2
        entry_page.cpp
        about_dialog.h            # M2
        about_dialog.cpp
        manage_dictionaries_dialog.h   # M3
        manage_dictionaries_dialog.cpp
      modules/
        AccountAndLicense/        # M3, reserved: see LICENSING.md, not present in M0
      web/                       # M6: server-side web app, see section 7A
    tests/                      # Qt Test
      CMakeLists.txt
      tst_normalize.cpp
      tst_bundle.cpp
      tst_changelog.cpp
  scripts/
    dev-build.sh
    dev-run.sh
    snap-runtime-env.sh
  packaging/                    # M5, packaging last
    snap/
    flatpak/
    appimage/
  .github/
    workflows/
      ci.yml
```

Bundle file extension: **`.odict`** (zstd-compressed SQLite). Decompressed on install to `<appdata>/dictionaries/<dict_id>/<version>/dict.sqlite`.

Only `core` and `app` exist in M0. `services/`, `platform/`, `models/`, `ui/`, `packaging/`, `web/` and `modules/AccountAndLicense/` are created in their milestone, not before; no dead code.

---

## 4. Canonical dictionary format

### 4.1 SQLite schema (one file per dictionary)

```sql
PRAGMA journal_mode = OFF;   -- read-only at runtime
PRAGMA page_size = 4096;

CREATE TABLE meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
-- required keys: dict_id, name, source_lang, target_lang, version, schema_version,
--                publisher, license, license_url, attribution, entry_count, built_at

CREATE TABLE entries (
  id             INTEGER PRIMARY KEY,
  headword       TEXT NOT NULL,          -- display form
  headword_norm  TEXT NOT NULL,          -- lowercased, NFKC, diacritics stripped (see normalize.py)
  lang           TEXT NOT NULL,          -- BCP-47 of the headword
  frequency      INTEGER,                -- 1..5 or NULL (Collins-style band)
  preview        TEXT NOT NULL,          -- first sense, plain text, <= 160 chars, precomputed
  sort_key       BLOB                    -- ICU collation key for locale-correct ordering
);
CREATE INDEX idx_entries_norm ON entries(headword_norm);
CREATE INDEX idx_entries_sort ON entries(sort_key);

CREATE TABLE pronunciations (
  id        INTEGER PRIMARY KEY,
  entry_id  INTEGER NOT NULL REFERENCES entries(id),
  ipa       TEXT,
  region    TEXT,                        -- e.g. "US", "UK", NULL
  audio_ref TEXT                         -- reserved; NULL in v1
);
CREATE INDEX idx_pronunciations_entry ON pronunciations(entry_id);

CREATE TABLE senses (
  id         INTEGER PRIMARY KEY,
  entry_id   INTEGER NOT NULL REFERENCES entries(id),
  ordinal    INTEGER NOT NULL,           -- 1-based display order
  pos        TEXT,                       -- normalized POS tag (see section 4.4)
  pattern    TEXT,                       -- grammar pattern, e.g. "ADV with cl/group"; NULL for open sources
  label      TEXT,                       -- pragmatic/usage label, e.g. "vagueness", "informal"
  definition TEXT NOT NULL,              -- restricted HTML (section 4.3)
  definition_plain TEXT NOT NULL         -- stripped text for FTS
);
CREATE INDEX idx_senses_entry ON senses(entry_id, ordinal);

CREATE TABLE examples (
  id          INTEGER PRIMARY KEY,
  sense_id    INTEGER NOT NULL REFERENCES senses(id),
  ordinal     INTEGER NOT NULL,
  text        TEXT NOT NULL,             -- restricted HTML
  translation TEXT                       -- for bilingual dictionaries
);
CREATE INDEX idx_examples_sense ON examples(sense_id, ordinal);

CREATE TABLE forms (
  form       TEXT NOT NULL,              -- inflected/variant form, e.g. "ran"
  form_norm  TEXT NOT NULL,
  entry_id   INTEGER NOT NULL REFERENCES entries(id),
  tag        TEXT                        -- "past", "plural", "alt-spelling", ...
);
CREATE INDEX idx_forms_norm ON forms(form_norm);
CREATE INDEX idx_forms_entry ON forms(entry_id);

CREATE TABLE relations (                 -- synonyms, antonyms, see-also
  entry_id   INTEGER NOT NULL REFERENCES entries(id),
  sense_id   INTEGER REFERENCES senses(id),
  type       TEXT NOT NULL,              -- "synonym" | "antonym" | "see" | "derived"
  target     TEXT NOT NULL               -- headword text (may not exist in this dict)
);
CREATE INDEX idx_relations_entry ON relations(entry_id);

CREATE VIRTUAL TABLE fts USING fts5(
  headword, definition_plain, example_text,
  content='',                            -- contentless; we store ids in rowid mapping
  tokenize='unicode61 remove_diacritics 2'
);
CREATE TABLE fts_map (rowid INTEGER PRIMARY KEY, entry_id INTEGER NOT NULL);
```

Schema versioning: `meta.schema_version` = `1`. Client refuses bundles with a higher major version.

### 4.2 Canonical JSONL (converter output -> build input)

One JSON object per entry:

```json
{
  "headword": "perhaps",
  "lang": "en",
  "frequency": 3,
  "pronunciations": [{"ipa": "pəˈhæps", "region": "UK"}],
  "senses": [
    {
      "pos": "adv",
      "pattern": "ADV with cl/group",
      "label": "vagueness",
      "definition": "You use <b>perhaps</b> to express uncertainty...",
      "examples": [{"text": "Perhaps she was right.", "translation": null}]
    }
  ],
  "forms": [{"form": "perhapses", "tag": "plural"}],
  "relations": [{"type": "synonym", "target": "maybe"}]
}
```

All fields except `headword`, `lang`, `senses[].definition` are optional. `build.py` validates against `schema.py` and computes `headword_norm`, `preview`, `sort_key`, `definition_plain`.

### 4.3 Restricted HTML subset (DECISION)

Allowed tags in `definition`, `examples.text`, `examples.translation`:

`<b> <i> <u> <sub> <sup> <br> <span class="pos|label|pattern|hw"> <a href="lex:HEADWORD">`

- No attributes other than `class` on `<span>` and `href` on `<a>`.
- `href` must be `lex:` scheme; the client resolves it as an in-app lookup.
- Everything else is stripped by `html_subset.py`; converters must produce output that passes validation with zero warnings in CI.

### 4.4 Normalized POS tags

`noun verb adj adv pron prep conj interj det num particle prefix suffix phrase abbr other`

Converters map source-specific tags to this set. Unknown maps to `other`, logged.

### 4.5 Headword normalization

1. Unicode NFKC
2. Case-fold
3. Strip combining diacritics **only** for scripts where that is standard (Latin, Cyrillic, Greek). Never strip for Devanagari, Arabic, Hebrew, Thai, CJK: matras/harakat are meaningful.
4. Collapse whitespace
5. `sort_key` is computed **only in the pipeline** (an ICU collation key for the `source_lang` locale) and stored in the bundle at build time; the client compares it bytewise and never recomputes it (ADR-006).

The same normalization function must exist in Python (`pipeline/omnipipe/normalize.py`) and in C++ (`app/src/core/normalize.cpp`) for query-time normalization; `sort_key` is pipeline-only (step 5 does not run in C++). The exact algorithm is ADR-005. A shared test vector file `tests/normalize_vectors.json` is read by both the Python and the C++ tests.

---

## 5. Bundle packaging & catalog

### 5.1 `manifest.json` (inside every bundle + listed in catalog)

```json
{
  "dict_id": "wikt-hi-en",
  "name": "Hindi to English (Wiktionary)",
  "source_lang": "hi",
  "target_lang": "en",
  "kind": "bilingual",
  "version": "2026.09.1",
  "schema_version": 1,
  "publisher": "Wiktionary contributors",
  "license": "CC-BY-SA-4.0",
  "license_url": "https://creativecommons.org/licenses/by-sa/4.0/",
  "attribution": "Data from Wiktionary via kaikki.org, (c) Wiktionary contributors, CC BY-SA 4.0",
  "entry_count": 48213,
  "size_compressed": 4180000,
  "size_installed": 21300000,
  "sha256": "...",
  "url": "https://cdn.example.com/dicts/wikt-hi-en/2026.09.1/wikt-hi-en.odict",
  "built_at": "2026-09-27T00:00:00Z",
  "source": {"converter": "kaikki", "dump_date": "2026-09-20"}
}
```

`dict_id` naming: `<source>-<src_lang>-<tgt_lang>`; monolingual uses `<source>-<lang>`.

### 5.2 `catalog.json`

```json
{
  "catalog_version": 1,
  "generated_at": "...",
  "dictionaries": [ { "...manifest..." } ]
}
```

Client fetches this on "Manage dictionaries" open and at most once per 24h in background. Diff against installed set, then show Available / Installed / Update available.

### 5.3 Install flow (client)

1. Download `.odict` to temp with resume support (`Range` header)
2. Verify `sha256`
3. zstd-decompress to `<appdata>/dictionaries/<dict_id>/<version>/dict.sqlite`
4. Open, verify `meta.schema_version` and `meta.dict_id`
5. Register in `installed.json`; remove older version dir if any
6. Emit `bundleInstalled(dict_id)`

---

## 6. Pipeline (`pipeline/`)

### 6.1 Converter contract (`converters/base.py`)

```python
class Converter(ABC):
    source_id: str                     # "kaikki", "freedict", ...
    def fetch(self, cache_dir: Path) -> Path: ...        # download/refresh dump
    def dictionaries(self) -> list[DictSpec]: ...        # which dict_ids this source can emit
    def iter_records(self, spec: DictSpec) -> Iterator[Entry]: ...  # canonical Entry objects
```

Each converter is independently runnable: `python -m omnipipe.converters.kaikki --dict wikt-hi-en --out out/`.

### 6.2 Source-specific notes

| Source | Format | Yields | Licence | Notes |
|--------|--------|--------|---------|-------|
| **Kaikki** (kaikki.org) | JSONL per Wiktionary edition | Hundreds of `X to en` from English Wiktionary; `en to X` + `X to X` from other editions | CC BY-SA 4.0 | Highest priority. Fields map almost 1:1: `senses[].glosses`, `senses[].examples`, `sounds[].ipa`, `forms[]`, `synonyms[]`. Filter by `lang_code`. |
| **FreeDict** | TEI XML (P5) | ~150 bilingual pairs | mostly GPL / CC | Good for `en to X`. Use `lxml` iterparse; dumps are large. |
| **WordNet** (Open English WordNet) | XML / LMF | `en` monolingual + relations | CC BY 4.0 | Rich synonym/antonym data, feeds the `relations` table. |
| **CC-CEDICT** | text, one line per entry | `zh to en` | CC BY-SA 4.0 | Trivial parser. Store traditional+simplified as `forms`. |
| **JMdict** | XML | `ja to en` (+ other targets) | CC BY-SA 4.0 | Readings feed `pronunciations`; kanji variants feed `forms`. |
| **KEngDic** | SQL/CSV | `ko to en` | MPL 2.0 | |
| **StarDict community** | .ifo/.idx/.dict.dz | thousands | varies/unclear | **v2, opt-in "unofficial" tier only** after licence review. |

### 6.3 Build commands

```
make fetch SOURCE=kaikki
make build DICT=wikt-hi-en          # -> out/wikt-hi-en/dict.sqlite
make package DICT=wikt-hi-en        # -> out/wikt-hi-en/wikt-hi-en.odict + manifest.json
make catalog                        # -> out/catalog.json
make publish                        # rclone/s3 sync out/ -> CDN   (OPEN: which provider)
make all                            # everything, parallel via GNU make -j
```

### 6.4 Pipeline quality gates (CI)

- `html_subset` validation: zero stripped tags per converter run, else fail
- `entry_count > 0` and `preview` non-empty for every entry
- Round-trip test: build, then open with the C++ `Bundle` class in a test, then look up 20 known headwords
- Schema migration test when `schema_version` bumps

---

## 7. Qt client (`app/`)

### 7.1 Stack
- Qt 6.11 (Widgets; QML dropped, see D14)
- C++20, CMake, `FetchContent` or system packages for: `sqlite3`, `zstd`, `ICU` (via Qt's bundled ICU or system)
- SQLite is accessed through the sqlite3 C API behind a small RAII wrapper (`core/sqlite_db`), not QtSql (ADR-003): this guarantees FTS5, read-only open flags, no SQL driver plugin to ship, and the same library on every platform
- Qt Test for unit tests

### 7.2 Core behaviours

**Search (mirrors Samsung UX):**
- As-you-type: prefix match on `headword_norm` (and `forms.form_norm`) across all open bundles, debounced 80 ms, top N per bundle, merged and grouped by dictionary -> `ResultsModel` with section headers
- Empty query shows history
- Exact match wins, then prefix, then FTS full-text (definitions/examples) as a "Also found in definitions" section
- Wildcards: `?` and `*` translate to SQL `LIKE` on `headword_norm` (`_` / `%`)
- Spell suggestion: if zero results, run FTS5 `spellfix1` **(OPEN: bundle spellfix1 or implement Damerau-Levenshtein over a headword sample)**
- "All ▼" dictionary filter dropdown restricts fan-out to one bundle

**Entry view:**
- `EntryRenderer` produces HTML from `entries + pronunciations + senses + examples + relations` using a single Qt-StyleSheet-friendly template; sections with no data are omitted
- Visual structure replicates Samsung/Collins: headword (accent colour) + frequency ◆ + IPA; `N POS` heading per sense; `[pattern]` in green; definition; `label` in a bordered box; examples as ◇ list in grey
- `lex:` links resolve to an in-app lookup; external links disabled
- Star button adds a favorite; every opened entry is added to history

**Manage dictionaries:**
- Tabs/sections: My dictionaries / Available, with source/target filter dropdowns (as in Samsung)
- Row: name, publisher (grey), size, action button (download / progress ring / delete / update)
- Multiple concurrent downloads allowed (max 2)

**About:**
- Renders `attribution` line for every installed bundle + app's own open-source notices

### 7.3 Performance targets
- Cold open with 50 bundles installed: < 1 s (open SQLite lazily on first query; keep LRU of 10 open connections)
- Keystroke to results paint: < 50 ms with 10 bundles
- Memory: < 150 MB with 10 bundles open

### 7.4 Threading
- All SQLite access on a dedicated `QThread` per bundle-manager (SQLite connections are per-thread); UI thread only receives result structs via signals
- Downloads and decompression on `QThreadPool`

---

## 7A. Web application (`web/`), M6, built last

### 7A.1 Purpose
Public dictionary website, **everything rendered server-side**, monetized with display ads (Google AdSense or similar). Organic search is the traffic model, so every entry must be a crawlable, fast, canonical URL.

### 7A.2 Architecture
- Consumes the exact same `.odict` bundles as the desktop app; **no second data path**. A deploy = sync bundles from CDN + reload.
- **Entry pages are statically pre-generated** at bundle-build time (`web/generate/`) and served from CDN/object storage. Millions of small HTML files; near-zero compute cost, best Core Web Vitals.
- A small dynamic server (Go preferred; FastAPI acceptable) handles only: search-as-you-type API, "did you mean", dictionary index/catalog pages, and fallback rendering for any entry not yet pre-generated.
- Rendering template = same structure and CSS classes as the desktop `EntryRenderer` (section 7.2). Port the template, not the logic; both must render identical HTML for the same entry (add a golden-file test).
- Cross-dictionary result page mirrors the desktop "All" view: one query, grouped results from every dictionary that has the word.

### 7A.3 URL scheme
```
/                                   home + search
/dictionaries                       catalog
/<dict_id>/                         dictionary index (A-Z / script-aware browse)
/<dict_id>/<headword-slug>          entry page (canonical)
/search?q=...&dict=...              SSR results page (noindex)
/api/suggest?q=...                  JSON, rate-limited
```
Slug: URL-safe form of `headword`; keep original Unicode (percent-encoded) rather than transliterating, so Devanagari/CJK URLs stay meaningful.

### 7A.4 SEO / compliance requirements (non-negotiable)
- Per-page **attribution + licence footer** from `manifest.attribution` (CC BY-SA obligation; also what AdSense reviewers look for)
- `sitemap.xml` index split per dictionary, regenerated on every bundle rebuild
- `<link rel="canonical">`, `hreflang` across language-pair variants, schema.org `DefinedTerm` / `DefinedTermSet` JSON-LD
- Search results and API pages `noindex`
- Ads: reserved slot sizes to avoid CLS; no ads on `noindex` pages
- Basic bot-abuse controls on `/api/suggest` (rate limit by IP, no scraping of full bundles via API)

### 7A.5 Differentiation (product requirement, not optional)
Wiktionary-derived dictionary sites are a crowded, low-RPM category and risk being classed as thin/duplicate content. The web version must ship with at least:
1. **Indian-language focus**: Hindi, Bengali, Tamil, Telugu, Marathi, Gujarati, Punjabi, Kannada, Malayalam pairs with **Latin-script transliteration search** (type `kadachit` to get कदाचित्)
2. **Cross-dictionary single-page results** (all sources for a word on one URL)
3. **App funnel**: every page links to the desktop app; the app's About/catalog links back to the site

### 7A.6 Web-specific OPEN questions
- Go vs FastAPI for the dynamic server
- Hosting: static pages on object storage + CDN (which provider) vs a VPS with nginx
- Domain / brand shared with the desktop app or separate
- Ad network beyond AdSense (Ezoic/Mediavine thresholds): decide after traffic exists

---

## 8. Milestones

| M | Deliverable | Acceptance |
|---|-------------|------------|
| **M0** | Repo scaffold, `schema.py`, `html_subset.py`, `normalize.py` + shared test vectors, `build.py` producing a valid `dict.sqlite` from hand-written JSONL | CI green; C++ `Bundle` test opens it and looks up 3 words |
| **M1** | Kaikki converter producing `wikt-hi-en`, `wikt-en` (monolingual), `wikt-es-en`; `package.py`; `catalog.py` | Three `.odict` bundles + `catalog.json` served from a local HTTP server |
| **M2** | Qt client: search page, results grouped by dictionary, entry page, history/favorites | Manual test against M1 bundles |
| **M3** | Manage-dictionaries dialog + downloader + install flow + About | Install/update/delete cycle works from local catalog |
| **M4** | FreeDict, WordNet, CC-CEDICT, JMdict converters; catalog > 100 dictionaries; publish to CDN | Real catalog reachable from the app |
| **M5** | Packaging: Snap + Flatpak + AppImage; CI release workflow | Installable on a clean Ubuntu |
| **M6** | Web app (section 7A): static entry-page generator, SSR server with search API, sitemaps, attribution footer, transliteration search for Indian languages, ad slots | Site live on a domain; Google Search Console shows entry pages indexed; golden-file test proves web and desktop render identical entry HTML |
| **v2** | DAWG/perfect-hash index, spell suggestion, MeCab/Jieba tokenization for CJK lookup, Windows/macOS builds, StarDict import (unofficial tier) | N/A |

Start with **M0, then M1, then M2** in that order; M1 before M2 so the client is developed against real data. **M6 is deliberately last**: do not start web work until M5 is shipped.

---

## 9. Open questions (OPEN, ask owner before deciding)

1. ~~Widgets vs QML for the UI~~ RESOLVED: Qt Widgets (D14).
2. CDN/object store provider for bundle hosting (and monthly bandwidth budget)
3. ~~App name and `dict_id` prefix conventions~~ RESOLVED: app name Omnidict, identity `com.ktechpit.omnidict` (D17); `dict_id` naming stays `<source>-<src_lang>-<tgt_lang>` (section 5.1) and is unaffected by the app name.
4. Spell-suggestion implementation (spellfix1 vs custom)
5. Whether to ship an initial English monolingual bundle inside the installer or require a first download
6. Telemetry: none by default, confirm

---

## 10. Agent instructions

The repository's actual `CLAUDE.md` lives at the repo root and is the binding set of agent instructions for this project; it is not duplicated here.
