# Content sources

Per-source notes from `DOCS/PLAN.md` section 6.2. **No content source is added to the pipeline
without a documented licence in this file.** Kaikki is first, in M1; the rest follow in M4.

| Source | Format | Yields | Licence | Dump URL | Status |
|---|---|---|---|---|---|
| Kaikki (kaikki.org) | gzipped JSONL per language of English Wiktionary | Hundreds of `X to en` pairs from English Wiktionary; `en to X` and `X to X` pairs from other Wiktionary editions | CC BY-SA 4.0 | `https://kaikki.org/dictionary/<Language>/kaikki.org-dictionary-<Language>.jsonl.gz` | in use (M1): `wikt-hi-en`, `wikt-es-en`, `wikt-en` |
| FreeDict | TEI XML (P5) | About 150 bilingual pairs, good coverage for `en to X` | mostly GPL / CC, varies per dictionary | to be recorded when the converter is written | planned (M4) |
| WordNet (Open English WordNet) | XML / LMF | `en` monolingual entries plus synonym/antonym relations | CC BY 4.0 | to be recorded when the converter is written | planned (M4) |
| CC-CEDICT | plain text, one line per entry | `zh to en` | CC BY-SA 4.0 | to be recorded when the converter is written | planned (M4) |
| JMdict | XML | `ja to en` (and other targets) | CC BY-SA 4.0, EDRDG licence | `http://ftp.edrdg.org/pub/Nihongo/JMdict_e.gz` | in use (M4): `jmdict-ja-en` |
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

## JMdict notes

Converter: `pipeline/omnipipe/converters/jmdict.py`, run as
`python -m omnipipe.converters.jmdict --dict jmdict-ja-en` or `make build DICT=jmdict-ja-en`.
There is exactly one dictionary here, `jmdict-ja-en` (`ja -> en`, bilingual).

- **Licence and attribution.** JMdict is CC BY-SA 4.0 under the EDRDG licence
  (https://www.edrdg.org/edrdg/licence.html), which requires a specific acknowledgement rather
  than leaving wording up to the user; the attribution field uses the EDRDG's own "For a
  Software Package" sample wording from https://www.edrdg.org/edrdg/sample.html, singularized
  to the one file this converter reads: "This package uses the JMdict dictionary file. This
  file is the property of the Electronic Dictionary Research and Development Group, and is
  used in conformance with the Group's licence."
- **Dump.** A single gzipped XML file, `JMdict_e.gz` (English glosses only), fetched with
  `If-Modified-Since` (compared against the cached file's mtime, since JMdict has no separate
  sidecar) into `pipeline/sources/jmdict/` (git-ignored). The bundle version is `YYYY.MM.N`
  from the dump's own "JMdict created: YYYY-MM-DD" comment, not the HTTP `Last-Modified` date.
- **Entities (DECISION).** The dump's internal DTD declares one XML entity per part-of-speech,
  usage, field and dialect code (`<!ENTITY n "noun (common) (futsuumeishi)">`,
  `<!ENTITY v5k "Godan verb with 'ku' ending">`, `<!ENTITY uk "word usually written using kana
  alone">`, ...). `xml.etree.ElementTree.iterparse` streams the whole file including that
  internal subset, so `expat` expands every entity reference to its declared value
  automatically; `<pos>&n;</pos>` parses with `.text` already resolved to "noun (common)
  (futsuumeishi)", and there is no supported way to keep the short code instead. Losing the
  code would break part-of-speech mapping, so the converter separately reads just the header
  bytes (before `<JMdict>`) and regex-extracts every `<!ENTITY code "value">` declaration into
  a `value -> code` table; because every entity's expansion text is unique, looking up a
  resolved element's `.text` in that table recovers the original code. The same header read
  also pulls the creation-date comment used for the bundle version.
- **Headword and forms.** The headword is the first `<keb>` (kanji form), or the first `<reb>`
  (reading) when the entry has no kanji form (e.g. `ねこ`). Remaining kebs become `forms`
  tagged "variant", every reb becomes a form tagged "reading", and every reb is additionally
  romanized with PyICU's `Any-Latin; Latin-ASCII` transform (chosen over separate
  `Hiragana-Latin`/`Katakana-Latin` transforms because readings routinely mix katakana,
  hiragana and the chouon mark, and this one transform handles all of them) as a form tagged
  "romanization", so Latin input such as "taberu" finds 食べる after headword normalization.
- **Senses.** Each `<sense>`'s `<gloss>` texts join with "; " as the definition; a sense with
  no gloss text is dropped. `<example>` is essentially unused in this dump (two occurrences in
  a ~45 MB sample, both DTD comment text) so no example sentences are emitted.
- **Parts of speech.** Mapped to the schema set by prefix: `n`/`n-pr`/`n-t`/`n-adv` -> noun,
  any `v*` -> verb, any `adj-*` -> adj, any `adv*` -> adv, `pn` -> pron, `prt` -> particle,
  `conj` -> conj, `int` -> interj, `num` -> num. Everything else (`ctr`, `suf`, `pref`,
  `n-pref`, `n-suf`, `exp`, `aux`/`aux-v`/`aux-adj`, `cop`, `unc`, ...) maps to `other` and is
  counted in the build output; on the 2026-09-28 dump the counts were `exp` 14,828, `n-suf`
  960, `suf` 686, `pref` 367, `ctr` 342, `n-pref` 212, `aux-v` 207, `aux` 54, `unc` 25, `cop`
  21, `aux-adj` 13. When a sense carries several `<pos>` tags the first one that maps to
  something other than `other` wins; `vt`/`vi` among them additionally set the sense `pattern`
  to "transitive"/"intransitive" (JMdict encodes transitivity as extra `<pos>` tags, not a
  separate field).
- **Labels.** `<misc>`, `<field>` and `<dial>` become the sense `label`. Most entity values are
  already short and readable ("colloquial", "computing", "Kansai-ben") and are used as-is; a
  small override table shortens the handful of long ones (`uk` -> "usually kana", `hon` ->
  "honorific", `hum` -> "humble", `pol` -> "polite", `X` -> "rude or X-rated"). `<s_inf>` notes
  join the same label.
- **Relations.** `<xref>` and `<ant>` become "see"/"antonym" relations on the sense. JMdict
  joins a target keb, reb and/or sense number with a katakana middle dot when a plain word is
  ambiguous ("丸・まる・1"); only the first, dot-free segment is kept as the relation target,
  since that is always the headword being pointed at.
- **Frequency.** `ke_pri`/`re_pri` priority codes across all kanji and reading elements become
  the entry `frequency`: `news1`/`ichi1`/`spec1`/`gai1` -> 5, `news2`/`ichi2`/`spec2`/`gai2` ->
  4, a standalone `nf01`-`nf24` -> 3, a standalone `nf25`-`nf48` -> 2 (in practice an `nf` tag
  always accompanies a `news` tag, so this is a fallback), any other priority code -> 1, no
  priority code at all -> no frequency.
- **Size.** 2026-09-28 dump: `JMdict_e.gz` 10.1 MB, 218,840 entries, `dict.sqlite` 126 MB
  installed.

