# Architecture Decision Records

Short, numbered, append-only. Format: Context, Decision, Consequences. Headings use a colon,
not a dash.

---

## ADR-001: Kit conventions override PLAN.md file naming and `.ui` usage (2026-09-27)

**Status.** Accepted.

**Context.** `DOCS/PLAN.md` was written before the project adopted the reusable
`rewrite-kit` conventions (`/home/commander/DCode/rewrite-kit`). The kit's `CODING_STANDARDS.md`
and `PLAYBOOK.md` already settle file naming, widget construction and library layering for a
Qt 6/C++20 desktop app; re-deriving those rules for Omnidict would just reinvent them.

**Decision.** Where the kit's conventions and `PLAN.md`'s originally-suggested file names
disagree, the kit wins: snake_case file names named after their primary class (`bundle.h`,
`main_window.cpp`, not `Bundle.h`/`MainWindow.cpp`), no `.ui` files (widgets are built in
code), one static library per directory layer so the linker enforces the dependency arrows,
`QT_NO_KEYWORDS`/`QT_NO_CAST_FROM_ASCII`/`QT_USE_QSTRINGBUILDER` defined globally, and PMF-only
signal/slot connects. `DOCS/CODING_STANDARDS.md` is the binding document day to day.

**Consequences.** `PLAN.md` section 3 has been rewritten to match. Any future document that
repeats a stale (`PascalCase.cpp`, `.ui`) example should be corrected on sight rather than
treated as an exception.

---

## ADR-002: Identity and paths derive from `app/src/app/version.h.in` only (2026-09-27)

**Status.** Accepted.

**Context.** Identity strings (application name, display name, organization, domain, desktop
id) tend to leak into multiple files and drift out of sync when the app is renamed or rebranded.

**Decision.** `app/src/app/version.h.in` is the single source for: application name
`omnidict`, display name `Omnidict`, organization `ktechpit`, domain `ktechpit.com`, and
desktop/AppStream id `com.ktechpit.omnidict`. Every other file (CMake, packaging manifests,
About dialog, settings paths) reads these constants rather than repeating the literals.

**Consequences.** A rebrand or a new build flavour touches one file. CMake configures
`version.h` from `version.h.in` so the values are available to C++ at compile time.

---

## ADR-003: SQLite through the sqlite3 C API behind a small RAII wrapper (2026-09-27)

**Status.** Accepted.

**Context.** Qt ships a SQL module (QtSql) that could open dictionary bundles, but it requires
a SQL driver plugin to be present at runtime, does not guarantee FTS5 support in the plugin
build shipped by a given distribution, and offers no clean way to force read-only, immutable
open flags.

**Decision.** The app opens dictionary bundles and the user-data database through the raw
`sqlite3` C API, wrapped in a small RAII class, `core/sqlite_db` (`SqliteDb`, `SqliteStatement`).
No other file may call `sqlite3_*` directly or construct a `QSqlDatabase`.

**Consequences.** Guaranteed FTS5 (linked directly, not via a system plugin), explicit
read-only/immutable open flags for installed bundles, no SQL driver plugin to ship in the snap
or Flatpak, and the same library and behaviour on every platform. The cost is writing and
testing a thin wrapper ourselves instead of using QtSql's API.

---

## ADR-004: Threading: SQLite worker thread and download thread pool are approved exceptions (2026-09-27)

**Status.** Accepted.

**Context.** `DOCS/CODING_STANDARDS.md` section 5 bans threads "unless measured", to keep the
app simple. Two specific needs are already known from `PLAN.md` section 7.4: SQLite
connections are per-thread and must not block the GUI thread, and downloads/decompression are
I/O-bound work that must not block it either.

**Decision.** A dedicated `QThread` per bundle-manager for SQLite access, and a `QThreadPool`
for downloads and zstd decompression, are pre-approved exceptions to the "no threads unless
measured" rule. Nothing else may add a thread without its own measurement and justification.

**Consequences.** These two are not built in M0; they arrive with the services that need them
(M2 for the SQLite worker, M3 for downloads). The UI thread only ever receives result structs
over signals, never touches SQLite or the network directly.

---

## ADR-005: Normalization uses ICU on both sides (2026-09-27)

**Status.** Accepted.

**Context.** Headword lookup must behave identically whether the query is normalized in the
Python pipeline (building `headword_norm`) or in the C++ client (normalizing what the user
typed). Both need the same Unicode-aware case folding and diacritic handling.

**Decision.** Normalization is implemented with ICU on both sides: PyICU in `pipeline/omnipipe/normalize.py`,
ICU4C in `app/src/core/normalize.cpp`. The algorithm, in order:

1. NFKC.
2. ICU case fold.
3. NFD.
4. Drop nonspacing marks (general category Mn) whose base character's script is Latin,
   Cyrillic or Greek.
5. NFC.
6. Collapse runs of Unicode `White_Space` to one space and trim.

`tests/normalize_vectors.json` is the contract: both test suites read it and must agree on
every vector.

**Consequences.** Known risk: the SDK/runtime snap ships ICU 74, while the pipeline host may
have a newer ICU (78, as of this writing), so Unicode versions can differ for recently added
characters. The shared test vectors stick to long-stable characters to avoid flaking on that
skew, and from M1 the pipeline's ICU version is recorded per bundle (a `meta` key) so a
mismatch is diagnosable later.

---

## ADR-006: `sort_key` is computed only in the pipeline (2026-09-27)

**Status.** Accepted.

**Context.** Locale-correct ordering needs an ICU collation key. If both the pipeline and the
client computed it, a difference between the two ICU versions (see ADR-005) would silently
reorder entries between a rebuild and a client update.

**Decision.** `sort_key` is computed once, at build time, in the pipeline: an ICU collation
key for the dictionary's `source_lang` locale, stored as a `BLOB` in `entries.sort_key`. The
client only ever compares `sort_key` values bytewise (`ORDER BY sort_key`); it never
recomputes or re-derives one.

**Consequences.** The client's ICU version can never affect ordering. Changing the collation
algorithm requires rebuilding affected bundles, not shipping a new client.

---

## ADR-007: Licence model (2026-09-27)

**Status.** Accepted.

**Context.** Omnidict is open source but reserves a path for a future Ktechpit account/licensing
module, matching the kit's "Model B" precedent (Playlist Downloader).

**Decision.** The program is GPL-3.0-or-later. A reserved path, `app/src/modules/AccountAndLicense/`,
`app/src/services/licensing/` and `app/src/core/licensing/` (not present until M3), is licensed
under `LicenseRef-Ktechpit-Licensing-Module` instead, with a GPLv3 section 7 additional
permission so official builds may combine the two. The tree is REUSE compliant via `REUSE.toml`;
no per-file SPDX headers are required outside the reserved paths. Dictionary content licences
are a separate matter, carried per bundle (`PLAN.md` D10) and are a legal attribution
obligation, not a code licence.

**Consequences.** `LICENSING.md`, `REUSE.toml` and `LICENSES/` describe and enforce this split.
Any file added under the reserved paths must carry the licensing-module SPDX identifier, not GPL.

---

## ADR-008: Dependencies (2026-09-27)

**Status.** Accepted.

**Context.** Every third-party dependency has a cost (build complexity, packaging, security
surface) and needs a documented reason and licence.

**Decision.** App: Qt 6.11, SQLite (FTS5), ICU, and zstd (from M3). Pipeline: PyICU and
zstandard. Dev-only: ruff, mypy, pytest. All are listed with version, licence and how they are
obtained in `THIRD_PARTY.md`. No new third-party dependency is added without an ADR and a
`THIRD_PARTY.md` row.

**Consequences.** `THIRD_PARTY.md` is kept current as a condition of merging any change that
adds a dependency.

---

## ADR-009: Dev builds use the KDE snap SDK, run against the kf6-core24 runtime (2026-09-27)

**Status.** Accepted.

**Context.** The app ships as a snap built on Qt 6.11 against the `kf6-core24` content snap.
For dev/prod parity, what compiles locally should be exactly what the shipped snap runs
against, including the exact Qt point release (6.11.1 as of this writing).

**Decision.** `scripts/dev-build.sh` builds against the `kde-qt6-core24-sdk` snap (Qt 6.11.1);
`scripts/dev-run.sh` runs the result against the `kf6-core24` runtime snap, using the same
libraries the shipped snap uses at runtime.

**Consequences.** Contributors install both snaps once (`sudo snap install kde-qt6-core24-sdk
kf6-core24`). A local system-Qt build is not supported as the primary workflow; CI uses the
same SDK/runtime pair.

---

## ADR-010: Monorepo: CMake root is `app/`, not the repo root (2026-09-27)

**Status.** Accepted.

**Context.** `pipeline/` (Python/uv) and `app/` (Qt/CMake) are two independent build systems
sharing one repository and one `tests/` directory of shared fixtures and vectors.

**Decision.** The CMake project root is `app/` (`app/CMakeLists.txt`), not the repository
root. Root-level `Makefile` targets (`make fixture`, `make test`, `make lint`) drive both
build systems and know to `cd`/point into `app/` and `pipeline/` as needed. Scripts and CI
account for the CMake root not being the repo root.

**Consequences.** IDEs and CI must be pointed at `app/` for CMake configuration, not the repo
root. This keeps the Python tooling (`pyproject.toml`, `uv.lock`) from ever being mistaken for
part of the CMake tree, and vice versa.

---

## ADR-011: App icon and brand colours (2026-09-27)

**Status.** Accepted.

**Context.** Flathub, the Snap Store and desktop launchers all need an app icon, a symbolic
variant and a pair of brand colours. The owner's `flathub-icon` recipe
(`/home/commander/DCode/flathub-icon`) encodes Flathub's icon quality rules and checks them.

**Decision.** The icon is a closed book (the dictionary) with a lookup badge (a magnifier),
drawn on the GNOME 128 px grid: blue cover `#3584e4` with a darker spine and a visible page
block, orange badge `#ffa348`. The symbolic icon is the same book in one colour at 16 px.
Brand colours: light `#ffd6a5`, dark `#8f4a0f` (warm, complementary to the blue icon). The
sources are `app/src/resources/icons/hicolor/scalable/apps/com.ktechpit.omnidict.svg` and
`.../symbolic/apps/com.ktechpit.omnidict-symbolic.svg`; the PNGs next to them (16 to 512 px)
are rendered from those with the recipe's `build_icon_set.py`, and all of its checks pass.

**Consequences.** Any change to the icon edits the SVGs and re-renders the PNGs with the same
script, then re-runs its checks. The metainfo written in M5 carries
`<icon type="stock">com.ktechpit.omnidict</icon>` and the two `<branding>` colours above; the
snap's `icon:` points at the 512 px PNG or the scalable SVG.

---

## ADR-012: Child-table indexes added within schema version 1 (2026-09-27)

**Status.** Superseded in part (2026-09-27): the owner asked for the strict reading, so the
indexes ship with `schema_version` 2 together with ADR-013's suggestion table.

**Context.** PLAN 4.1 indexed `examples`, `pronunciations`, `forms` and `relations` only for
lookup, not by their parent. Opening one entry of the 884k-entry `wikt-en` bundle then scanned
600k examples once per sense (38 ms each), so an entry with 30 senses took over a second.

**Decision.** Four indexes join the DDL: `examples(sense_id, ordinal)`,
`pronunciations(entry_id)`, `forms(entry_id)`, `relations(entry_id)`. `schema_version` stays
1: no table, column or meaning changed, so every version-1 reader reads the new bundles as it
read the old ones, only faster, and no bundle had been published. The version exists to stop a
reader that cannot understand a bundle; an index never makes that happen.

**Consequences.** `DOCS/schema.md`, PLAN 4.1 and `build.py` carry the same DDL.
`test_client_queries_never_scan_a_table` checks with `EXPLAIN QUERY PLAN` that every query the
client runs (`app/src/core/bundle.cpp`) is answered from an index, so a new client query needs
a matching entry there. Should the owner prefer a strict reading of the rule, the fix is a
one-line bump to 2 before the first catalog is published.

---

## ADR-013: Spell suggestion with our own trigram and edit-distance suggester (2026-09-27)

**Status.** Accepted by the owner, 2026-09-27 (PLAN 9 question 4), with `schema_version` 2.

**Context.** Zero-result searches should offer "Did you mean". PLAN suggested SQLite's
spellfix1 extension or a custom Damerau-Levenshtein method. Both were measured on the three
M1 bundles; the numbers and the full comparison are in `DOCS/spell-suggestion.md`.

**Decision.** A `suggest` FTS5 table (built-in `trigram` tokenizer) over each bundle's distinct
`headword_norm`, built by the pipeline; the client takes candidates from it and ranks them by
Damerau-Levenshtein distance in C++, on the lookup thread, only on zero results. The bundle
gains a table, so `schema_version` goes to 2 together with ADR-012's indexes. Reason:
spellfix1 returned unrelated words for every Devanagari query, needs a vendored C extension
that no system SQLite ships, and makes bundles unreadable by tools without it; the custom
method matched it on English and Spanish, works for any script, and adds no dependency.

**Consequences.** A latency test holds suggestions under 100 ms on English (the
prototype's worst case was 337 ms in Python, so the query needs the tuning described in the
research note). Shared cases in `tests/suggest_cases.json` keep Python and C++ in step.

---

## ADR-014: Qt Svg and Lucide glyphs for the UI (2026-09-27)

**Status.** Accepted (follows the approved mocks, which draw every control with Lucide glyphs).

**Context.** The approved mocks use one icon family in both themes and in several states
(normal, muted, accent, disabled, the warm favourite star). Desktop icon themes differ per
distribution and are missing on Windows and macOS.

**Decision.** The rewrite kit's approach: Lucide SVGs, normalised to `stroke="#000000"`, live in
`app/src/resources/icons/ui/` and `ui::icons` tints them at runtime with Qt Svg, caching per
name, colour, size and pixel ratio. Qt Svg is a Qt module from the same SDK and runtime, so it
adds no new supplier. The filled favourite star is the outline glyph with a fill.

**Consequences.** New glyphs come in with the kit's `icons/pull.sh`. The ISC notice (and MIT for
the Feather-derived glyphs) ships in `app/src/resources/icons/ui/LICENSE`, is declared in
`REUSE.toml`, and is listed in About's open-source notices.

---

## ADR-015: Qt Network for downloads, zstd for `.odict` decompression (2026-09-27)

**Status.** Accepted.

**Context.** M3 (PLAN.md 5.3, 7.2) needs to fetch `catalog.json` and `.odict` files over HTTP
with resume support, and to decompress `.odict` bundles (zstd, matching
`pipeline/omnipipe/package.py`) into an installed `dict.sqlite`. `DOCS/DECISIONS.md` ADR-008
already named zstd as an app dependency "from M3"; this ADR is that arrival, plus the network
module it was always going to need alongside it.

**Decision.** `services/dictionary_manager.cpp` uses `Qt6::Network` (`QNetworkAccessManager`,
`QNetworkReply`, a `Range` header for resume) for every network call the app makes; nothing else
in the tree opens a socket. `core/installer.cpp` links the system/runtime zstd (via the project's
own `app/cmake/FindZstd.cmake`, since neither CMake nor the SDK ships one) and decompresses with
the streaming `ZSTD_decompressStream` C API, matching `zstandard`'s output on the pipeline side.
Dev builds get zstd's header from the `kde-qt6-core24-sdk` snap and its library from the
`core24` runtime farm the same way SQLite already does (`scripts/dev-build.sh`,
`app/cmake/SnapSdkWorkaround.cmake`), so the RPATH never reaches core24's glibc.

**Consequences.** `app/CMakeLists.txt` gains `find_package(Qt6 ... Network)` and
`find_package(Zstd REQUIRED)`; `THIRD_PARTY.md`'s zstd row is no longer "from M3" but in use, and
gains a Qt Network row. `core/` stays free of Qt Network (ADR/CODING_STANDARDS layering:
`services/` may use Qt Network, `core/` may not), so `core/installer.cpp` is still testable
without a network stack, only a file on disk.

## ADR-016: Dictionaries hosted on GitHub Releases, built by GitHub Actions (2026-09-28)

**Status.** Accepted (owner, 2026-09-28): one rolling release, source links instead of
re-hosted sources. The workflow is written (`.github/workflows/dictionaries.yml`) and runs
only when started by hand; nothing is published until the owner does (PLAN 9 question 2).

**Context.** The catalogue is 297 dictionaries and 1.45 GB of `.odict` files today (median
2.3 MB, largest `wikt-en` 275 MB), about 4 to 5 GB once the Wiktionary languages above 1,000
senses are added. Download traffic, not storage, is what a paid host would charge for.
Options compared: GitHub Releases (no bandwidth charge, 2 GB per file, 1,000 files per
release), Cloudflare R2 (10 GB free, no egress fees, card on file), Hugging Face datasets,
Internet Archive and SourceForge (free but slow or redirect-heavy), GitHub or Cloudflare
Pages (too small per file for bundles, fine for `catalog.json`).

**Decision.**
- `.odict` files and `catalog.json` are served from GitHub Releases. Cloudflare R2 is the
  fallback if GitHub ever limits the traffic; each catalogue entry carries its own URL, so a
  move is a new catalogue, not a new app.
- GitHub Actions builds the bundles and attaches them to the release; the owner's machine is
  not in the upload path (its disk could not hold a full build anyway).
- Upstream sources (FreeDict `.src.tar.xz`, Kengdic's `kengdic.tsv`) are not re-hosted in the
  release; this keeps the file count down. Whether a link to the upstream source is enough
  for the GPL, AGPL and MPL dictionaries is an open point, see Consequences.

**The rolling release.** One release, tag `dictionaries`, holds every bundle as
`<dict_id>-<version>.odict` (`package.py --flat-urls`, since a release has no folders) and the
`catalog.json` the app reads, at a URL that never changes:
`https://github.com/<owner>/<repo>/releases/download/dictionaries/catalog.json`. A run:
1. drops every bundle the published catalogue no longer lists (those are two runs old);
2. builds the dictionaries asked for (all by default) in chunks, one dictionary at a time
   per runner (`make ci-bundle-<id>`), uploading each bundle as it is done, replacing a file
   of the same name;
3. merges the new manifests into the published catalogue (`catalog.py --previous
   --manifests-only`: a dictionary that was not rebuilt, or whose chunk failed, keeps its
   entry) and uploads it.
Bundles of the previous catalogue stay until the next run, so an app that read it recently
can still download them. 623 dictionaries today; a monthly run that renews the 328 Wiktionary
ones peaks near 950 files, under GitHub's 1,000 per release. If that ever gets tight, the
Wiktionary bundles move to a release of their own.

**Consequences.**
- The Wiktionary cut-off dropped from 10,000 to 1,000 senses (DOCS/sources.md
  "Wiktionary languages").
- The app follows GitHub's redirect to its file host (Qt 6's default redirect policy); a
  test with a redirecting server is to be added before publishing.
- Copyleft: for GPL-3.0 and AGPL-3.0 dictionaries distributed online, an offer to supply the
  source on request is not enough on its own (GPLv3 section 6(b) covers physical products
  only); section 6(d) allows pointing to a copy on a third party's server, such as FreeDict's
  own release, as long as it stays available. Settled (owner, 2026-09-28): every bundle
  records its upstream source in `source_url` (schema 3) and the About card links it.
- Needs the GitHub repository, which the owner has deferred.
