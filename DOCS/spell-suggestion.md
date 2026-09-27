# Spell suggestion: spellfix1 or our own (PLAN 9, question 4)

Status: **accepted by the owner on 2026-09-27** (ADR-013): our own suggester, `schema_version` 2. Measured on
2026-09-27 against the three real bundles built in M1.

## The feature

When a search finds nothing in any dictionary, show "Did you mean" with up to five
headwords close to what was typed (mock: `mocks/main-no-results.html`). It runs only on zero
results, never on every keystroke, and never for wildcard queries.

## The two candidates

**spellfix1** is an SQLite extension (`ext/misc/spellfix.c` in the SQLite source tree, public
domain). It is a virtual table: you insert every headword, and a query like
`SELECT word FROM sf WHERE word MATCH 'dictionery' AND top=5` returns the nearest ones. It
works by turning each word into an ASCII "sounds like" key (a transliteration plus an
English-oriented phonetic hash), finding words that share the start of that key, and ranking
them by a weighted edit distance.

**Our own** (as measured): a second FTS5 table in each bundle using SQLite's built-in
`trigram` tokenizer (FTS5 is already required), holding every distinct `headword_norm`. A
query fetches words that share three-letter pieces with what was typed, then C++ ranks them by
Damerau-Levenshtein distance (letters added, dropped, changed or swapped) on the normalized
text. It needs no new dependency, and the pieces are Unicode characters, so it does not care
about the script.

## Measurements

Distinct headwords: English 857,931, Spanish - English 110,538, Hindi - English 23,029.
Scripts and data in the session scratchpad; the query sets are typical misspellings (20
English, 10 Spanish, 10 Hindi). "Found" means the intended word is in the top five.

| | spellfix1 | Trigram + distance (ours) |
|---|---|---|
| English found | 13 of 20 | 13 of 20 |
| Spanish found | 9 of 10 | 9 of 10 |
| Hindi found | **0 of 10** (returns unrelated words starting with अ) | 9 of 10 (the miss is a 2-letter word, which is an exact match anyway) |
| Extra size, English | 42 MB (5% of the 809 MB bundle) | 36 MB (4.4%) |
| Extra size, Spanish / Hindi | 5.3 MB / 1.1 MB | 4.4 MB / 1.1 MB |
| Build time, English | 9 s | 5.5 s |
| Query time, English (median / worst) | 33 ms / 120 ms | 88 ms / 337 ms (Python prototype) |
| Query time, Spanish / Hindi (median) | 3 ms / 7 ms | 13 ms / 0.5 ms |

Where they differ on English: spellfix1's phonetic key gets `fonetic` to `phonetic`, which the
trigram method misses (the two words share almost no letters at the start); the trigram method
gets `kwestion` to `question`, which spellfix1 misses. Several "misses" on both sides are
common misspellings that Wiktionary lists as headwords of their own (`recieve`, `wierd`), so
those searches never reach the suggester: the user gets the entry "misspelling of receive".

The trigram query time is the weak spot. Most of it is SQLite ranking every word that shares
a common piece such as "ion". The implementation would skip the most common pieces (FTS5
exposes their counts), count matches itself instead of asking SQLite to rank, and run the
distance in C++ (the prototype did it in Python). A test would hold it under 100 ms on
English on the lookup thread. I have not built that version, so this target is a claim to
prove, not a result.

## Comparison

| Topic | spellfix1 | Ours |
|---|---|---|
| Accuracy, Latin script | Good; better on sound-alike errors (ph and f) | Good; better on errors at the start of a word |
| Accuracy, other scripts | Cyrillic and Greek are transliterated and work; Devanagari, Arabic, CJK and most Indian scripts do not work at all | Works for every script, because it compares characters |
| Speed | Fast (tens of ms) with no extra work | Needs the tuning above to be fast on English |
| Storage | About 5% of a bundle | About 4 to 5% of a bundle |
| Dependencies | Must vendor `spellfix.c` (about 3,000 lines of C): no system SQLite ships it (checked: the snap runtime, Arch, and Python's). Needs an ADR and a `THIRD_PARTY.md` row | None new: FTS5 trigram is in every SQLite we use (3.45 in the runtime) |
| Pipeline | Python must load a compiled C extension to build the table; our uv Python allows it, but that is one more native build to keep working in CI | Plain SQL from Python's standard `sqlite3` |
| Opening a bundle elsewhere | Any tool without the extension (sqlite3 shell, DB browsers, the M6 web server unless it also vendors it) errors with "no such module: spellfix1" on that table | Any SQLite with FTS5 reads it |
| Web app (M6, PLAN D12) | The server must vendor it too | Same table, same query, in any language |
| Tuning | Edit costs are configurable per language through a cost table, but the phonetic key is English-only | Every rule is ours: per-script costs, a sound-alike key for Latin-script languages can be added later |
| Maintenance | SQLite maintains it, but it lives in `ext/misc` (outside the core, rarely changed); we still own the build glue on three platforms | About 200 lines of C++ and 30 of Python that we own and test, with shared test cases on both sides like the normalization vectors |
| Licence | Public domain, no obligations | Our code, GPL-3.0-or-later like the rest |
| Schema | New virtual table in the bundle: `schema_version` 2 | New FTS5 table in the bundle: `schema_version` 2 |

Both approaches change the bundle, so either needs a `schema_version` bump. ADR-012 (the four
indexes) is waiting on the same question: one bump to version 2 before the first catalogue is
published can carry both.

## Recommendation

**Our own trigram + Damerau-Levenshtein suggester, stored in the bundle, with `schema_version`
2.** The deciding reason is scripts: Omnidict is meant for Indian languages (PLAN 7A lists
transliteration search for them), and spellfix1 returns nonsense for Devanagari, which is
worse than no suggestion. The costs we take on are small: no new dependency, bundles that any
SQLite can open, the same table for the web app, and about the same size. What we give up is
spellfix1's sound-alike matching for English, which can be added later as a second key for
Latin-script dictionaries, and its speed, which we must earn with the tuning above and a
latency test.

Alternatives if you prefer:
- **spellfix1 for Latin, Cyrillic and Greek dictionaries only**, no suggestions elsewhere:
  fastest to build, but two code paths and still a vendored C file.
- **Build the suggestion table on the user's machine after install** instead of shipping it:
  no schema change and no larger downloads, but 5.5 s of work per English install on every
  device and a second file per dictionary to manage.
- **Defer to v2** as PLAN section 8 first scheduled: the mock stays, the feature waits.

## If approved, the work

1. ADR-013 accepted; `schema_version` 2 with ADR-012's indexes; `DOCS/schema.md` updated.
2. Pipeline: `suggest` FTS5 table (trigram, `detail=none`) over distinct `headword_norm`;
   tests.
3. Client: `core::Suggester` with shared test cases (`tests/suggest_cases.json`) run by
   Python and C++; runs on the lookup thread only on zero results; latency test on the
   English bundle.
4. UI from the approved `main-no-results.html` mock.
