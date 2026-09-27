# Content sources

Per-source notes from `DOCS/PLAN.md` section 6.2. **No content source is added to the pipeline
without a documented licence in this file.** Kaikki is first, in M1; the rest follow in M4.

| Source | Format | Yields | Licence | Dump URL | Status |
|---|---|---|---|---|---|
| Kaikki (kaikki.org) | gzipped JSONL per language of English Wiktionary | Hundreds of `X to en` pairs from English Wiktionary; `en to X` and `X to X` pairs from other Wiktionary editions | CC BY-SA 4.0 | `https://kaikki.org/dictionary/<Language>/kaikki.org-dictionary-<Language>.jsonl.gz` | in use (M1): `wikt-hi-en`, `wikt-es-en`, `wikt-en` |
| FreeDict | TEI XML (P5) | About 150 bilingual pairs, good coverage for `en to X` | mostly GPL / CC, varies per dictionary | to be recorded when the converter is written | planned (M4) |
| WordNet (Open English WordNet) | XML / LMF | `en` monolingual entries plus synonym/antonym relations | CC BY 4.0 | to be recorded when the converter is written | planned (M4) |
| CC-CEDICT | plain text, one line per entry | `zh to en` | CC BY-SA 4.0 | to be recorded when the converter is written | planned (M4) |
| JMdict | XML | `ja to en` (and other targets) | CC BY-SA 4.0 | to be recorded when the converter is written | planned (M4) |
| KEngDic | SQL/CSV | `ko to en` | MPL 2.0 | to be recorded when the converter is written | planned (M4) |
| StarDict community | `.ifo`/`.idx`/`.dict.dz` | Thousands of dictionaries, licence varies or is unclear per file | varies/unclear, needs review per dictionary | to be recorded when the converter is written | planned (v2, opt-in "unofficial" tier only, after licence review) |

## Rule

No content source is wired into `pipeline/omnipipe/converters/` without a row in this table
recording its licence. A source whose licence is "varies/unclear" (StarDict) stays out of the
default catalog until each dictionary drawn from it has been reviewed individually.

## Kaikki notes

Converter: `pipeline/omnipipe/converters/kaikki.py`, run as
`python -m omnipipe.converters.kaikki --dict <dict_id>` or `make build DICT=<dict_id>`.

- **Dumps.** One gzipped file per language, fetched with `If-Modified-Since` into
  `pipeline/sources/kaikki/` (git-ignored) with a `<file>.json` sidecar recording the URL and
  `Last-Modified`. The dump date is that `Last-Modified` date and gives the bundle version
  `YYYY.MM.N`. Dumps are streamed; nothing is decompressed to disk.
- **Entries.** Kaikki writes one record per word, part of speech and etymology. Consecutive
  records for one word merge into one entry, senses in source order.
- **Inflections.** Records whose every sense is an inflection ("form-of") are skipped
  (`--include-form-of` keeps them): the lemma lists the same inflections as `forms`, which is
  how a lookup of `comí` finds `comer`. For Spanish this drops 687k of 811k records.
- **Forms.** Inflection-table bookkeeping (`table-tags`, `inflection-template`, `class`,
  `error-*`) is dropped, duplicates removed, at most 150 per entry. Romanizations are kept with
  the tag `romanization`, so Latin-script input finds Hindi entries (`kadacit` finds
  कदाचित्). This is plain diacritic-free matching of Wiktionary's romanization, not the
  phonetic transliteration search planned for the web app (PLAN 7A.5).
- **Senses.** The innermost gloss, or parent plus child when the parent ends with `:`
  ("inflection of X: oblique plural"). Wiki links inside a gloss become `lex:` links. Grammar
  tags (gender, transitivity, countability, ...) become `pattern`, usage tags (formal, slang,
  archaic, ...) become `label`. Proper nouns map to `noun` with the pattern `proper noun`.
- **Parts of speech.** Mapped to the schema set; anything unmapped becomes `other` and is
  counted in the build output (characters, symbols, punctuation, interfixes).
- **Examples.** Short examples before quotations, at most 3 per sense, none over 300
  characters. Kaikki's bold offsets become `<b>`; a romanization follows the example on its
  own line in italics; the English translation is the example's `translation`.
- **Pronunciations.** Phonemic before phonetic, regions mapped from tags (US, UK, ...), at
  most 4.
- **Relations.** Entry and sense synonyms and antonyms (20 each), derived terms (30) and
  related terms as `see` (20). Thesaurus pages and self references are dropped.
- **Size.** 2026-09-25 dumps: `wikt-hi-en` 23,029 entries, 57 MB installed, 10.9 MB as
  `.odict`; `wikt-es-en` 113,073 entries, 169 MB, 38 MB. `wikt-en` is built without the final
  VACUUM (`--no-vacuum`), which would need a temporary copy of the whole database; on Spanish
  that costs 2.8% installed and 8% compressed.

