# Canonical dictionary schema reference

Generated from `DOCS/PLAN.md` section 4. This is the reference used by both `pipeline/omnipipe`
and `app/src/core`; if the two disagree, this file and `PLAN.md` are the tie-breaker, and
whichever side is wrong gets fixed.

`meta.schema_version` is currently **1**. Any change to the SQL schema below or to the
manifest fields (`DOCS/PLAN.md` section 5.1) bumps `schema_version` and updates this file in
the same commit. The client refuses to open a bundle with a higher major `schema_version` than
it understands.

## SQL DDL (one file per dictionary)

```sql
PRAGMA journal_mode = OFF;   -- read-only at runtime
PRAGMA page_size = 4096;

CREATE TABLE meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE entries (
  id             INTEGER PRIMARY KEY,
  headword       TEXT NOT NULL,          -- display form
  headword_norm  TEXT NOT NULL,          -- normalized form, see "Normalization" below
  lang           TEXT NOT NULL,          -- BCP-47 of the headword
  frequency      INTEGER,                -- 1..5 or NULL (Collins-style band)
  preview        TEXT NOT NULL,          -- first sense, plain text, <= 160 chars, precomputed
  sort_key       BLOB                    -- ICU collation key, pipeline-only, see ADR-006
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
  pos        TEXT,                       -- normalized POS tag, see "POS tag set" below
  pattern    TEXT,                       -- grammar pattern, e.g. "ADV with cl/group"; NULL for open sources
  label      TEXT,                       -- pragmatic/usage label, e.g. "vagueness", "informal"
  definition TEXT NOT NULL,              -- restricted HTML, see "Restricted HTML subset" below
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
  content='',                            -- contentless; ids kept in the rowid mapping
  tokenize='unicode61 remove_diacritics 2'
);
CREATE TABLE fts_map (rowid INTEGER PRIMARY KEY, entry_id INTEGER NOT NULL);
```

## Required `meta` keys

`dict_id`, `name`, `source_lang`, `target_lang`, `version`, `schema_version`, `publisher`,
`license`, `license_url`, `attribution`, `entry_count`, `built_at`. `build.py` refuses to
produce a bundle missing any of these.

Optional keys, written when known and ignored by readers that do not need them (adding one does
not change `schema_version`):

| Key | Meaning |
|---|---|
| `kind` | `monolingual` or `bilingual` |
| `icu_version`, `unicode_version` | the ICU and Unicode versions the pipeline normalized and collated with |
| `source_converter` | the converter that produced the entries, e.g. `kaikki` |
| `source_dump_date` | date of the source dump, `YYYY-MM-DD`; becomes `manifest.source.dump_date` |

## Canonical JSONL record (converter output, `build.py` input)

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

Field-by-field rules:

| Field | Required | Rules |
|---|---|---|
| `headword` | yes | Non-empty display form. |
| `lang` | yes | BCP-47 tag of the headword's language. |
| `frequency` | no | Integer 1 to 5 (Collins-style band) or omitted. |
| `pronunciations` | no | List; each entry needs at least `ipa` or is dropped. `region` is a free-text label (`"US"`, `"UK"`) or omitted. `audio_ref` is reserved, always `null` in v1. |
| `senses` | yes, at least one | Each sense needs `definition` (restricted HTML, non-empty after validation); `pos`, `pattern`, `label` and `examples` are optional. |
| `senses[].definition` | yes | Must pass `omnipipe.html_subset.validate()` with zero stripped tags. |
| `senses[].examples` | no | Each example needs `text` (restricted HTML); `translation` is optional, used for bilingual dictionaries. |
| `forms` | no | Each needs `form`; `tag` is optional free text ("past", "plural", "alt-spelling"). |
| `relations` | no | Each needs `type` (one of `synonym`, `antonym`, `see`, `derived`) and `target` (headword text; the target does not have to exist in this dictionary). |

`build.py` computes and adds, they are never present in the converter's JSONL output:
`headword_norm`, `preview` (first sense, plain text, truncated to 160 characters),
`sort_key`, `definition_plain` (per sense, stripped of markup for FTS).

## Restricted HTML subset

Allowed tags in `definition`, `examples.text` and `examples.translation`:

`<b> <i> <u> <sub> <sup> <br> <span class="pos|label|pattern|hw"> <a href="lex:HEADWORD">`

- No attributes other than `class` on `<span>` and `href` on `<a>`.
- `href` must use the `lex:` scheme; the client resolves it as an in-app lookup, never a real
  URL fetch.
- Everything else (any other tag, any other attribute, any `<script>`/`<style>`/event handler)
  is stripped by `html_subset.py`. A converter run that strips anything fails CI with zero
  tolerance: converters must already emit clean output.

## POS tag set

`noun verb adj adv pron prep conj interj det num particle prefix suffix phrase abbr other`

Converters map source-specific part-of-speech tags onto this fixed set. An unmapped tag
becomes `other` and is logged, never silently dropped.

## Normalization

See ADR-005 for the full rationale. The algorithm, run identically in `pipeline/omnipipe/normalize.py`
(PyICU) and `app/src/core/normalize.cpp` (ICU4C):

1. Unicode NFKC.
2. ICU case fold.
3. Unicode NFD.
4. Drop nonspacing marks (general category Mn) whose base character's script is Latin,
   Cyrillic or Greek. Devanagari, Arabic, Hebrew, Thai and CJK marks are never dropped:
   matras/harakat and similar marks are meaningful there.
5. Unicode NFC.
6. Collapse runs of Unicode `White_Space` to one space and trim leading/trailing space.

`sort_key` (ADR-006) is a separate step, run **only in the pipeline**: an ICU collation key
for the dictionary's `source_lang` locale, stored as a `BLOB`. The client compares `sort_key`
bytewise and never recomputes it, so the client's ICU version cannot affect ordering.

`tests/normalize_vectors.json` is the shared contract both test suites read; any change to the
algorithm changes the vectors in the same commit.

## FTS layout

One `fts` row per **sense**, not per entry:

- `headword`: the entry's headword.
- `definition_plain`: that sense's `definition_plain` (the stripped text of that one sense,
  not the whole entry).
- `example_text`: that sense's example texts, stripped of markup and joined with `\n`.

`fts_map.rowid` is the `fts` table's own rowid; `fts_map.entry_id` is the owning entry's id, so
a full-text hit is resolved back to an entry by joining `fts_map` on `fts`'s rowid.

## Schema versioning

`schema_version` is **1**. Any change to the SQL DDL above or to the manifest fields
(`DOCS/PLAN.md` section 5.1) is a schema change: it bumps `schema_version` and updates this
file, in the same commit that makes the change. The client refuses to open a bundle whose
`meta.schema_version` is higher than the version it was built to understand.
