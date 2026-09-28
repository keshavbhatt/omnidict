# Content sources

Per-source notes from `DOCS/PLAN.md` section 6.2. **No content source is added to the pipeline
without a documented licence in this file.** Kaikki is first, in M1; the rest follow in M4.

| Source | Format | Yields | Licence | Dump URL | Status |
|---|---|---|---|---|---|
| Kaikki (kaikki.org) | gzipped JSONL per language of English Wiktionary | Hundreds of `X to en` pairs from English Wiktionary; `en to X` and `X to X` pairs from other Wiktionary editions | CC BY-SA 4.0 | `https://kaikki.org/dictionary/<Language>/kaikki.org-dictionary-<Language>.jsonl.gz` | in use (M1): `wikt-hi-en`, `wikt-es-en`, `wikt-en` |
| FreeDict | TEI XML (P5) | About 150 bilingual pairs, good coverage for `en to X` | mostly GPL / CC, varies per dictionary | to be recorded when the converter is written | planned (M4) |
| WordNet (Open English WordNet) | XML / LMF | `en` monolingual entries plus synonym/antonym relations | CC BY 4.0 | to be recorded when the converter is written | planned (M4) |
| CC-CEDICT | plain text, one line per entry | `zh to en` | CC BY-SA 4.0 | `https://www.mdbg.net/chinese/export/cedict/cedict_1_0_ts_utf-8_mdbg.txt.gz` | in use (M4): `cedict-zh-en` |
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

## CC-CEDICT notes

Converter: `pipeline/omnipipe/converters/cedict.py`, run as
`python -m omnipipe.converters.cedict --dict cedict-zh-en` or `make build DICT=cedict-zh-en`.

- **Licence.** Confirmed from the dump's own header comment (`# Creative Commons
  Attribution-ShareAlike 4.0 International License`,
  `https://creativecommons.org/licenses/by-sa/4.0/`) and from `https://cc-cedict.org/wiki/`:
  CC BY-SA 4.0, publisher MDBG, "Referenced works: CEDICT - Copyright (C) 1997, 1998 Paul
  Andrew Denisowski".
- **Dump.** One gzipped UTF-8 text file (no per-language split), fetched with
  `If-Modified-Since` into `pipeline/sources/cedict/` (git-ignored) with a `<file>.json`
  sidecar recording the URL, `Last-Modified` and fetch time, same shape as Kaikki's. Unlike
  Kaikki, the bundle version does not come from that sidecar: CC-CEDICT stamps its own release
  date in the file (`#! date=2026-09-27T12:50:23Z`), so `bundle_version` reads that line
  instead, giving `YYYY.MM.N`.
- **Format.** One line per headword reading: `Traditional Simplified [pinyin] /gloss
  1/gloss 2/.../`. Lines starting with `#` are header/comment and skipped; a line that does not
  match the shape is skipped and counted (`ConversionStats.malformed`), never a hard failure.
- **Merging.** The headword is the Simplified form. Every line for the same Simplified form
  merges into one entry, in file order, whether the lines are adjacent or not: CC-CEDICT sorts
  by Traditional form, so two Simplified-identical entries (e.g. `裡` and `里`, both simplifying
  to `里`) can be far apart. A Simplified form with several Mandarin readings (e.g. `行`: hang2,
  heng2, xing2) also merges into one entry, one sense group per reading, in file order. This is
  a deliberate simplification: the entry does not record which senses belong to which reading;
  a reader tells them apart from the pinyin forms and the sense text itself.
- **Forms.** The Traditional form, tagged `traditional`, only when it differs from Simplified.
  Pinyin, tagged `pinyin`, converted from numbered tone digits to tone marks (`ni3 hao3` ->
  `nǐ hǎo`; `u:` and `v` both read as `ü`; tone `5` is neutral, no mark) and stored **twice**:
  once with its natural syllable spacing and once concatenated. `normalize_headword` strips
  diacritics but never removes spaces, so a spaced query (`ni hao`) only normalizes equal to
  the spaced form and a run-together query (`nihao`, or the toned `nǐhǎo` typed without a
  space) only to the concatenated one; storing both is what makes every common typing style
  find the entry. A single-syllable pinyin form is not duplicated (spaced == concatenated).
- **Pronunciation.** No `pronunciations` row: `schema.py`'s `Pronunciation.ipa` and the
  `entry-html.md` contract both treat that field as an IPA transcription, and pinyin is not
  IPA. Pinyin only appears as a `forms` entry (above). Worth a follow-up ADR if a client ever
  wants pinyin rendered as a first-class pronunciation line rather than a search alias.
- **Classifiers.** A `CL:` gloss (e.g. `CL:個|个[ge4]`) is not its own sense. CC-CEDICT always
  places it immediately after the sense it classifies, and a line can carry more than one
  (`棋 棋 [qi2] /chess/.../CL:盤|盘[pan2]/chess piece/CL:個|个[ge4],顆|颗[ke1]/`), so it becomes
  the `label` of the definition immediately before it on the same line, formatted
  `CL: 個/个 (gè)` (multiple classifiers on one gloss joined with `; `). A `CL:` with nothing
  before it on its line is dropped (CC-CEDICT never emits that shape).
- **Cross-references.** An embedded `TARGET[pinyin]` reference (`see 你好[ni3 hao3]`, `variant
  of X[...]`, `also written X|Y[...]`, `erhua variant of X[...]`, ...) becomes a `lex:` link on
  `TARGET` (the Simplified side when `TARGET` is `Traditional|Simplified`); the bracketed
  pinyin is dropped from the rendered text since the linked entry carries its own pinyin as a
  form.
- **Part of speech.** Not given by CC-CEDICT; left `null` on every sense.
- **Surnames.** `surname X` glosses are kept as ordinary senses, no special handling.
- **Size.** 2026-09-27 dump (`#! date=2026-09-27T12:50:23Z`, 125,127 source lines): 121,362
  entries, 0 malformed lines, 66 MB as `dict.sqlite` (built with the default VACUUM).

