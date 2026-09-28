# Content sources

Per-source notes from `DOCS/PLAN.md` section 6.2. **No content source is added to the pipeline
without a documented licence in this file.** Kaikki is first, in M1; the rest follow in M4.

| Source | Format | Yields | Licence | Dump URL | Status |
|---|---|---|---|---|---|
| Kaikki (kaikki.org) | gzipped JSONL per language of English Wiktionary | Hundreds of `X to en` pairs from English Wiktionary; `en to X` and `X to X` pairs from other Wiktionary editions | CC BY-SA 4.0 | `https://kaikki.org/dictionary/<Language>/kaikki.org-dictionary-<Language>.jsonl.gz` | in use (M1): `wikt-hi-en`, `wikt-es-en`, `wikt-en` |
| FreeDict | TEI XML (P5) | 291 bilingual pairs (of 305 listed), good coverage for `en to X` and `X to en` | per dictionary, free licences only, see notes | `https://freedict.org/freedict-database.json` | in use (M4): 291 dictionaries |
| WordNet (Open English WordNet) | XML / LMF (WN-LMF) | `en` monolingual entries plus synonym/antonym/hypernym relations | CC BY 4.0 | `https://github.com/globalwordnet/english-wordnet/releases/download/2025-edition/english-wordnet-2025.xml.gz` | in use (M4): `oewn-en` |
| CC-CEDICT | plain text, one line per entry | `zh to en` | CC BY-SA 4.0 | `https://www.mdbg.net/chinese/export/cedict/cedict_1_0_ts_utf-8_mdbg.txt.gz` | in use (M4): `cedict-zh-en` |
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

## Open English WordNet notes

Converter: `pipeline/omnipipe/converters/oewn.py`, run as
`python -m omnipipe.converters.oewn` or `make build DICT=oewn-en` (there is only one
dictionary, `oewn-en`, so `--dict` is optional).

- **Licence.** Confirmed in the upstream `LICENSE.md` and `README.md` (2025 edition):
  Creative Commons Attribution 4.0 International, derived from the Princeton WordNet under
  its own WordNet License. Attribution is required to both the Princeton WordNet and the
  Open English Wordnet team; both are named in `manifest.json`'s `attribution` field.
- **Dump.** One release asset, the compiled WN-LMF XML file
  (`english-wordnet-<edition>.xml.gz`, found from the GitHub releases page, currently the
  `2025-edition` release's `english-wordnet-2025.xml.gz`, not the `-plus` edition, which adds
  proper nouns out of scope here), fetched with `If-Modified-Since` into
  `pipeline/sources/oewn/` (git-ignored) with an `english-wordnet.xml.gz.json` sidecar
  recording the URL and `Last-Modified`. The dump date is that `Last-Modified` date and gives
  the bundle version `YYYY.MM.N`, the same scheme as Kaikki.
- **Parsing.** Stream-parsed with `xml.etree.ElementTree.iterparse`; every `LexicalEntry`
  precedes every `Synset` in the file (verified against the 2025 edition), so one pass
  collects both, resolving a synset's members and hypernym targets against the lexical
  entries already read (hypernym targets may point forward to a synset read later in the
  same pass and are resolved once the whole file has been read).
- **Entries.** One entry per written form (`Lemma/@writtenForm`); lexical entries that share
  a written form across parts of speech merge into one entry, senses grouped in the order
  noun, verb, adjective (`a` and `s`, adjective satellite, together), adverb.
- **Senses.** Each `Sense` becomes one sense; its definition is its synset's `Definition`
  (joined if a synset carries more than one, which is rare), its examples are the synset's
  `Example` elements, at most 3.
- **Relations.** A sense's synonyms are its synset's other members, at most 10, as `synonym`
  relations; `SenseRelation` of type `antonym` becomes an `antonym` relation, at most 10; a
  synset's `hypernym` relations become `see` relations, at most 5, pointing at the first
  member of the target synset.
- **Forms.** `Form` elements are the lexical entry's irregular inflections (OEWN's 2025
  edition does not tag them further), recorded with the tag `inflection`, deduplicated and
  excluding the headword itself.
- **Pronunciations.** `Pronunciation` elements are kept where the file has them (IPA plus a
  `@variety` code); `GB` is recorded as region `UK` to match the other converters, every
  other variety code (`US`, `CA`, `AU`, `IE`, `NZ`, `SG`, `ZA`) is kept as is.
- **Size.** 2025 edition (fetched 2026-02-04, `2026.02.1`): 135,969 lexical entries merge
  into 128,009 entries (185,129 senses, 441,690 relations, 37,396 pronunciations, 4,447
  forms), 99 MB installed, 28 MB as `.odict`.

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



## FreeDict notes

Converter: `pipeline/omnipipe/converters/freedict.py`, run as
`python -m omnipipe.converters.freedict --dict <dict_id>` or `make build DICT=<dict_id>`.

- **Index.** `https://freedict.org/freedict-database.json` lists every FreeDict dictionary
  (305 as of 2026-09-28) with one or more release artifacts; this converter uses the release
  whose `platform` is `"src"`, a `.src.tar.xz` holding the TEI P5 source. The index is fetched
  into `pipeline/sources/freedict/freedict-database.json` (git-ignored) with a `<file>.json`
  sidecar, refreshed with `If-Modified-Since` like Kaikki's dumps.
- **Eligibility policy.** A FreeDict dictionary is included only when both hold:
  1. it has at least 1000 headwords (the index's own `headwords` field);
  2. its TEI `teiHeader/fileDesc/publicationStmt/availability` names a licence
     `detect_license` can map to an SPDX id: a versioned GNU URL (GPL/AGPL/LGPL/GFDL) or a
     Creative Commons URL taken at face value; an *unversioned* GNU URL (FreeDict commonly
     links the bare `gpl.html`/`fdl.html` page) resolved from a version number named in the
     link's own text or the surrounding paragraph; or, with no link at all, a plain-text
     "Creative Commons Attribution ver." mention. "-or-later" is added when the text says so
     ("or later", "any later version", "and later"), else "-only". Anything else, including a
     bare "public domain" claim with no licence link, is excluded and counted, never guessed.
  `--list` prints the eligible dictionaries and, on stderr, an exclusion count by reason;
  `--list --markdown` prints the table below; `--list --ids-only` feeds
  `pipeline/freedict-dicts.txt` (read by `Makefile`'s `DICTS_freedict`).
  As of 2026-09-28: 291 of 305 dictionaries are eligible; 14 excluded for too few headwords,
  0 for licence (the 11 that first looked unclear all resolved once the unversioned-URL and
  plain-text passes were added; see `detect_license` in `freedict.py`).
- **Identity.** `dict_id` is `freedict-<src>-<tgt>` with ISO 639-1 codes where one exists
  (`_LANG_TABLE` in `freedict.py`), else the FreeDict/ISO 639-3 code (`ast`, `ckb`, `kha`,
  `kmr`, `rom`, `swh`->`sw`... the full table is in the converter). `name` is
  "Source-Target (FreeDict)" (e.g. "German-English (FreeDict)") with English language names;
  `kind` is always `bilingual`. `publisher` is the header's `author`/`editor`/maintainer names
  joined with "; " (the placeholder "[up for grabs]" is dropped), or "FreeDict contributors"
  when none remain. `attribution` is "title, publisher, licence".
- **Version.** The FreeDict edition as-is when it already fits the pipeline's version rule
  (dot-separated non-negative integers); otherwise its numeric groups joined by dots, e.g.
  `1.9-fd1` -> `1.9.1`, `2024.10.06+fd1` -> `2024.10.06.1` (`version_from_edition`).
- **Entries.** One `<entry>` per TEI element; consecutive entries sharing a headword merge into
  one, the same rule Kaikki uses (FreeDict emits `Bank.1`, `Bank.2`, `Bank.3` back to back for
  one headword's different senses/genders). `<gramGrp><pos>`/`<gen>` are entry-level and apply
  to every `<sense>` of that entry; `<gen>` becomes the sense `pattern` ("masculine",
  "feminine", "neuter"). A sense's definition is its `<cit type="trans"><quote>` translations
  joined by ", ", with a `<def>` element's text appended in parentheses when present; a sense
  with neither is dropped. `<usg>` becomes the sense `label`. `<cit type="example">` becomes an
  example, its nested `<cit type="trans">` the translation. `<xr type="syn"/"ant"/"see">`
  references become relations (anything else maps to `see`), the ref's visible text as the
  target. `<form><pron>` becomes a pronunciation only when it looks like IPA
  (`_looks_like_ipa`); FreeDict has no separate plain-transcription field, so a non-IPA-looking
  `<pron>` is dropped rather than guessed at.
- **Fetching.** `fetch_release` downloads a dictionary's `.src.tar.xz` into
  `pipeline/sources/freedict/<name>/`, verifies its sha512 against the index, and keeps only
  the current edition's file. `iter_records` streams the `.tei` member straight out of that
  tarball with `ET.iterparse`, never extracting it to disk; memory stays bounded by clearing
  each `<entry>` (and the parsed root) once it has been read, the same trick as `build.py`'s
  batched writes.
- **Size (2026-09-28 editions, sha512-verified sources).** `freedict-de-en` (`deu-eng`
  1.9-fd1) 501,895 entries, 18 MB source, 278 MB `dict.sqlite` (built with `--no-vacuum`, which
  a 500k-entry bundle needs); `freedict-en-de` (`eng-deu` 1.9-fd1) 458,699 entries, 228 MB;
  `freedict-en-hi` (`eng-hin` 1.6) 23,177 entries, 12 MB; `freedict-fr-en` (`fra-eng` 0.4.1) and
  `freedict-en-fr` (`eng-fra` 0.1.6) 8,417 and 8,799 entries, 2.5 MB each; `freedict-es-en`
  (`spa-eng` 0.3.1) 4,502 entries, 1.3 MB.

### Included dictionaries (eligible as of 2026-09-28)

| dict_id | dictionary | headwords | licence |
|---|---|---|---|
| freedict-af-de | Afrikaans-German | 3800 | GPL-2.0-or-later |
| freedict-af-en | Afrikaans-English | 5129 | GPL-2.0-or-later |
| freedict-ar-en | Arabic-English | 52996 | GPL-2.0-or-later |
| freedict-br-fr | Breton-French | 27034 | GPL-2.0-or-later |
| freedict-ca-en | Catalan-English | 23590 | CC-BY-SA-3.0 |
| freedict-ca-es | Catalan-Spanish | 27177 | CC-BY-SA-3.0 |
| freedict-ca-fi | Catalan-Finnish | 10395 | CC-BY-SA-3.0 |
| freedict-ca-fr | Catalan-French | 21681 | CC-BY-SA-3.0 |
| freedict-ca-it | Catalan-Italian | 13087 | CC-BY-SA-3.0 |
| freedict-ca-pl | Catalan-Polish | 10008 | CC-BY-SA-3.0 |
| freedict-ca-pt | Catalan-Portuguese | 11855 | CC-BY-SA-3.0 |
| freedict-ca-ru | Catalan-Russian | 10390 | CC-BY-SA-3.0 |
| freedict-ckb-kmr | Central Kurdish-Northern Kurdish | 7845 | GPL-2.0-or-later |
| freedict-cs-bg | Czech-Bulgarian | 16621 | CC-BY-SA-3.0 |
| freedict-cs-ca | Czech-Catalan | 17052 | CC-BY-SA-3.0 |
| freedict-cs-da | Czech-Danish | 15663 | CC-BY-SA-3.0 |
| freedict-cs-de | Czech-German | 17730 | CC-BY-SA-3.0 |
| freedict-cs-es | Czech-Spanish | 12308 | CC-BY-SA-3.0 |
| freedict-cs-fi | Czech-Finnish | 10505 | CC-BY-SA-3.0 |
| freedict-cs-fr | Czech-French | 15265 | CC-BY-SA-3.0 |
| freedict-cs-ga | Czech-Irish | 12813 | CC-BY-SA-3.0 |
| freedict-cs-id | Czech-Indonesian | 11792 | CC-BY-SA-3.0 |
| freedict-cs-ja | Czech-Japanese | 10335 | CC-BY-SA-3.0 |
| freedict-cs-nl | Czech-Dutch | 10383 | CC-BY-SA-3.0 |
| freedict-cs-no | Czech-Norwegian | 11164 | CC-BY-SA-3.0 |
| freedict-cs-pl | Czech-Polish | 10396 | CC-BY-SA-3.0 |
| freedict-cs-pt | Czech-Portuguese | 16783 | CC-BY-SA-3.0 |
| freedict-cs-ru | Czech-Russian | 10296 | CC-BY-SA-3.0 |
| freedict-cs-sv | Czech-Swedish | 11455 | CC-BY-SA-3.0 |
| freedict-cs-tr | Czech-Turkish | 15465 | CC-BY-SA-3.0 |
| freedict-cy-en | Welsh-English | 12630 | GPL-2.0-or-later |
| freedict-da-en | Danish-English | 5097 | GPL-3.0-or-later |
| freedict-de-bg | German-Bulgarian | 10557 | CC-BY-SA-3.0 |
| freedict-de-ca | German-Catalan | 15227 | CC-BY-SA-3.0 |
| freedict-de-cs | German-Czech | 19866 | CC-BY-SA-3.0 |
| freedict-de-da | German-Danish | 12645 | CC-BY-SA-3.0 |
| freedict-de-el | German-Greek | 10792 | CC-BY-SA-3.0 |
| freedict-de-en | German-English | 517534 | GPL-3.0-only |
| freedict-de-es | German-Spanish | 36744 | CC-BY-SA-3.0 |
| freedict-de-fi | German-Finnish | 13028 | CC-BY-SA-3.0 |
| freedict-de-fr | German-French | 59631 | CC-BY-SA-3.0 |
| freedict-de-ga | German-Irish | 13246 | CC-BY-SA-3.0 |
| freedict-de-id | German-Indonesian | 10131 | CC-BY-SA-3.0 |
| freedict-de-it | German-Italian | 4443 | GPL-2.0-or-later |
| freedict-de-ja | German-Japanese | 10203 | CC-BY-SA-3.0 |
| freedict-de-ku | German-Kurdish | 22567 | GPL-2.0-or-later |
| freedict-de-lt | German-Lithuanian | 10282 | CC-BY-SA-3.0 |
| freedict-de-nl | German-Dutch | 12784 | GPL-2.0-or-later |
| freedict-de-pl | German-Polish | 23168 | CC-BY-SA-3.0 |
| freedict-de-pt | German-Portuguese | 8722 | GPL-2.0-or-later |
| freedict-de-ru | German-Russian | 26809 | CC-BY-SA-3.0 |
| freedict-de-sv | German-Swedish | 46539 | CC-BY-SA-3.0 |
| freedict-de-tr | German-Turkish | 36219 | GPL-2.0-or-later |
| freedict-el-bg | Greek-Bulgarian | 12979 | CC-BY-SA-3.0 |
| freedict-el-ca | Greek-Catalan | 14414 | CC-BY-SA-3.0 |
| freedict-el-cs | Greek-Czech | 11884 | CC-BY-SA-3.0 |
| freedict-el-da | Greek-Danish | 11739 | CC-BY-SA-3.0 |
| freedict-el-de | Greek-German | 11417 | CC-BY-SA-3.0 |
| freedict-el-en | Greek-English | 41714 | CC-BY-SA-3.0 |
| freedict-el-es | Greek-Spanish | 10702 | CC-BY-SA-3.0 |
| freedict-el-fi | Greek-Finnish | 13556 | CC-BY-SA-3.0 |
| freedict-el-fr | Greek-French | 32246 | CC-BY-SA-3.0 |
| freedict-el-ga | Greek-Irish | 19124 | CC-BY-SA-3.0 |
| freedict-el-id | Greek-Indonesian | 18161 | CC-BY-SA-3.0 |
| freedict-el-it | Greek-Italian | 11318 | CC-BY-SA-3.0 |
| freedict-el-ja | Greek-Japanese | 12751 | CC-BY-SA-3.0 |
| freedict-el-la | Greek-Latin | 13060 | CC-BY-SA-3.0 |
| freedict-el-lt | Greek-Lithuanian | 14575 | CC-BY-SA-3.0 |
| freedict-el-nl | Greek-Dutch | 12236 | CC-BY-SA-3.0 |
| freedict-el-no | Greek-Norwegian | 11103 | CC-BY-SA-3.0 |
| freedict-el-pl | Greek-Polish | 12358 | CC-BY-SA-3.0 |
| freedict-el-pt | Greek-Portuguese | 13929 | CC-BY-SA-3.0 |
| freedict-el-ru | Greek-Russian | 14709 | CC-BY-SA-3.0 |
| freedict-el-sv | Greek-Swedish | 12411 | CC-BY-SA-3.0 |
| freedict-el-tr | Greek-Turkish | 10277 | CC-BY-SA-3.0 |
| freedict-en-af | English-Afrikaans | 6397 | GPL-2.0-or-later |
| freedict-en-ar | English-Arabic | 87424 | GPL-2.0-or-later |
| freedict-en-bg | English-Bulgarian | 37030 | CC-BY-SA-3.0 |
| freedict-en-ca | English-Catalan | 35163 | CC-BY-SA-3.0 |
| freedict-en-cs | English-Czech | 150004 | GPL-2.0-or-later |
| freedict-en-cy | English-Welsh | 12630 | GPL-2.0-or-later |
| freedict-en-de | English-German | 460315 | GPL-3.0-only |
| freedict-en-el | English-Greek | 20973 | GPL-2.0-or-later |
| freedict-en-es | English-Spanish | 64258 | CC-BY-SA-3.0 |
| freedict-en-fi | English-Finnish | 75586 | CC-BY-SA-3.0 |
| freedict-en-fr | English-French | 8799 | GPL-2.0-or-later |
| freedict-en-ga | English-Irish | 1359 | GPL-2.0-or-later |
| freedict-en-hi | English-Hindi | 25642 | GPL-2.0-or-later |
| freedict-en-hr | English-Croatian | 59194 | GPL-2.0-or-later |
| freedict-en-hu | English-Hungarian | 89679 | GPL-2.0-or-later |
| freedict-en-id | English-Indonesian | 15262 | CC-BY-SA-3.0 |
| freedict-en-it | English-Italian | 53258 | CC-BY-SA-3.0 |
| freedict-en-ja | English-Japanese | 38419 | CC-BY-SA-3.0 |
| freedict-en-ku | English-Kurdish | 10011 | CC-BY-SA-3.0 |
| freedict-en-la | English-Latin | 3026 | GPL-2.0-or-later |
| freedict-en-lt | English-Lithuanian | 6255 | GPL-2.0-or-later |
| freedict-en-nl | English-Dutch | 7714 | GPL-2.0-or-later |
| freedict-en-no | English-Norwegian | 11427 | CC-BY-SA-3.0 |
| freedict-en-pl | English-Polish | 16362 | GPL-3.0-or-later |
| freedict-en-pt | English-Portuguese | 15766 | GPL-2.0-or-later |
| freedict-en-ru | English-Russian | 62181 | CC-BY-SA-3.0 |
| freedict-en-sv | English-Swedish | 44077 | CC-BY-SA-3.0 |
| freedict-en-sw | English-Swahili | 1450 | GPL-2.0-or-later |
| freedict-en-tr | English-Turkish | 36589 | GPL-2.0-or-later |
| freedict-en-zh | English-Chinese | 26660 | CC-BY-SA-3.0 |
| freedict-eo-en | Esperanto-English | 63895 | CC-BY-3.0 |
| freedict-es-ast | Spanish-Asturian | 49252 | GPL-3.0-or-later |
| freedict-es-ca | Spanish-Catalan | 10080 | CC-BY-SA-3.0 |
| freedict-es-cs | Spanish-Czech | 10118 | CC-BY-SA-3.0 |
| freedict-es-de | Spanish-German | 21353 | GPL-2.0-or-later |
| freedict-es-en | Spanish-English | 4502 | GPL-2.0-or-later |
| freedict-es-fi | Spanish-Finnish | 11403 | CC-BY-SA-3.0 |
| freedict-es-fr | Spanish-French | 16273 | CC-BY-SA-3.0 |
| freedict-es-ga | Spanish-Irish | 11765 | CC-BY-SA-3.0 |
| freedict-es-id | Spanish-Indonesian | 10842 | CC-BY-SA-3.0 |
| freedict-es-it | Spanish-Italian | 10745 | CC-BY-SA-3.0 |
| freedict-es-pl | Spanish-Polish | 12015 | CC-BY-SA-3.0 |
| freedict-es-ru | Spanish-Russian | 11982 | CC-BY-SA-3.0 |
| freedict-es-sv | Spanish-Swedish | 11075 | CC-BY-SA-3.0 |
| freedict-es-tr | Spanish-Turkish | 10068 | CC-BY-SA-3.0 |
| freedict-fi-bg | Finnish-Bulgarian | 16253 | CC-BY-SA-3.0 |
| freedict-fi-ca | Finnish-Catalan | 21862 | CC-BY-SA-3.0 |
| freedict-fi-cs | Finnish-Czech | 12477 | CC-BY-SA-3.0 |
| freedict-fi-da | Finnish-Danish | 14080 | CC-BY-SA-3.0 |
| freedict-fi-de | Finnish-German | 12102 | CC-BY-SA-3.0 |
| freedict-fi-el | Finnish-Greek | 19398 | CC-BY-SA-3.0 |
| freedict-fi-en | Finnish-English | 42060 | CC-BY-SA-3.0 |
| freedict-fi-es | Finnish-Spanish | 10568 | CC-BY-SA-3.0 |
| freedict-fi-fr | Finnish-French | 12019 | CC-BY-SA-3.0 |
| freedict-fi-ga | Finnish-Irish | 15633 | CC-BY-SA-3.0 |
| freedict-fi-id | Finnish-Indonesian | 14025 | CC-BY-SA-3.0 |
| freedict-fi-it | Finnish-Italian | 14843 | CC-BY-SA-3.0 |
| freedict-fi-ja | Finnish-Japanese | 14742 | CC-BY-SA-3.0 |
| freedict-fi-la | Finnish-Latin | 10980 | CC-BY-SA-3.0 |
| freedict-fi-lt | Finnish-Lithuanian | 12962 | CC-BY-SA-3.0 |
| freedict-fi-nl | Finnish-Dutch | 14158 | CC-BY-SA-3.0 |
| freedict-fi-no | Finnish-Norwegian | 14350 | CC-BY-SA-3.0 |
| freedict-fi-pl | Finnish-Polish | 15057 | CC-BY-SA-3.0 |
| freedict-fi-pt | Finnish-Portuguese | 14591 | CC-BY-SA-3.0 |
| freedict-fi-sv | Finnish-Swedish | 17229 | CC-BY-SA-3.0 |
| freedict-fi-tr | Finnish-Turkish | 10317 | CC-BY-SA-3.0 |
| freedict-fr-bg | French-Bulgarian | 17900 | CC-BY-SA-3.0 |
| freedict-fr-br | French-Breton | 36017 | GPL-2.0-or-later |
| freedict-fr-ca | French-Catalan | 23678 | CC-BY-SA-3.0 |
| freedict-fr-cs | French-Czech | 19331 | CC-BY-SA-3.0 |
| freedict-fr-da | French-Danish | 13466 | CC-BY-SA-3.0 |
| freedict-fr-de | French-German | 48578 | CC-BY-SA-3.0 |
| freedict-fr-el | French-Greek | 14272 | CC-BY-SA-3.0 |
| freedict-fr-en | French-English | 8505 | GPL-2.0-or-later |
| freedict-fr-es | French-Spanish | 52659 | CC-BY-SA-3.0 |
| freedict-fr-fi | French-Finnish | 17179 | CC-BY-SA-3.0 |
| freedict-fr-ga | French-Irish | 12637 | CC-BY-SA-3.0 |
| freedict-fr-it | French-Italian | 67356 | CC-BY-SA-3.0 |
| freedict-fr-ja | French-Japanese | 16724 | CC-BY-SA-3.0 |
| freedict-fr-la | French-Latin | 10302 | CC-BY-SA-3.0 |
| freedict-fr-lt | French-Lithuanian | 12413 | CC-BY-SA-3.0 |
| freedict-fr-nl | French-Dutch | 9604 | GPL-2.0-or-later |
| freedict-fr-pl | French-Polish | 21689 | CC-BY-SA-3.0 |
| freedict-fr-pt | French-Portuguese | 26132 | CC-BY-SA-3.0 |
| freedict-fr-ru | French-Russian | 24739 | CC-BY-SA-3.0 |
| freedict-fr-sv | French-Swedish | 19515 | CC-BY-SA-3.0 |
| freedict-fr-tr | French-Turkish | 11628 | CC-BY-SA-3.0 |
| freedict-fr-zh | French-Chinese | 10947 | CC-BY-SA-3.0 |
| freedict-ga-en | Irish-English | 1185 | GPL-2.0-or-later |
| freedict-hr-en | Croatian-English | 79808 | GPL-2.0-or-later |
| freedict-hu-en | Hungarian-English | 139935 | GPL-2.0-only |
| freedict-is-en | Icelandic-English | 11219 | GPL-3.0-or-later |
| freedict-it-bg | Italian-Bulgarian | 20793 | CC-BY-SA-3.0 |
| freedict-it-ca | Italian-Catalan | 22148 | CC-BY-SA-3.0 |
| freedict-it-cs | Italian-Czech | 16819 | CC-BY-SA-3.0 |
| freedict-it-da | Italian-Danish | 18601 | CC-BY-SA-3.0 |
| freedict-it-de | Italian-German | 2924 | GPL-2.0-or-later |
| freedict-it-el | Italian-Greek | 20176 | CC-BY-SA-3.0 |
| freedict-it-en | Italian-English | 29660 | CC-BY-SA-3.0 |
| freedict-it-es | Italian-Spanish | 14278 | CC-BY-SA-3.0 |
| freedict-it-fi | Italian-Finnish | 21510 | CC-BY-SA-3.0 |
| freedict-it-ga | Italian-Irish | 17014 | CC-BY-SA-3.0 |
| freedict-it-id | Italian-Indonesian | 14546 | CC-BY-SA-3.0 |
| freedict-it-ja | Italian-Japanese | 20066 | CC-BY-SA-3.0 |
| freedict-it-lt | Italian-Lithuanian | 12754 | CC-BY-SA-3.0 |
| freedict-it-nl | Italian-Dutch | 11235 | CC-BY-SA-3.0 |
| freedict-it-no | Italian-Norwegian | 11240 | CC-BY-SA-3.0 |
| freedict-it-pl | Italian-Polish | 19191 | CC-BY-SA-3.0 |
| freedict-it-pt | Italian-Portuguese | 12762 | CC-BY-SA-3.0 |
| freedict-it-ru | Italian-Russian | 14137 | CC-BY-SA-3.0 |
| freedict-it-sv | Italian-Swedish | 18483 | CC-BY-SA-3.0 |
| freedict-it-tr | Italian-Turkish | 18882 | CC-BY-SA-3.0 |
| freedict-ja-de | Japanese-German | 109546 | CC-BY-SA-3.0 |
| freedict-ja-en | Japanese-English | 173747 | CC-BY-SA-3.0 |
| freedict-ja-fr | Japanese-French | 14891 | CC-BY-SA-3.0 |
| freedict-ja-ru | Japanese-Russian | 6742 | CC-BY-SA-3.0 |
| freedict-kha-en | Khasi-English | 2280 | GPL-2.0-or-later |
| freedict-ku-de | Kurdish-German | 22035 | GPL-2.0-or-later |
| freedict-ku-en | Kurdish-English | 5208 | GPL-2.0-or-later |
| freedict-ku-tr | Kurdish-Turkish | 24377 | GPL-2.0-or-later |
| freedict-la-de | Latin-German | 5500 | GPL-3.0-or-later |
| freedict-la-en | Latin-English | 2305 | GPL-2.0-or-later |
| freedict-lt-en | Lithuanian-English | 7031 | GPL-2.0-or-later |
| freedict-mk-bg | Macedonian-Bulgarian | 4546 | GPL-3.0-or-later |
| freedict-nl-bg | Dutch-Bulgarian | 12665 | CC-BY-SA-3.0 |
| freedict-nl-ca | Dutch-Catalan | 11793 | CC-BY-SA-3.0 |
| freedict-nl-cs | Dutch-Czech | 11396 | CC-BY-SA-3.0 |
| freedict-nl-da | Dutch-Danish | 10221 | CC-BY-SA-3.0 |
| freedict-nl-de | Dutch-German | 17224 | GPL-2.0-or-later |
| freedict-nl-el | Dutch-Greek | 10051 | CC-BY-SA-3.0 |
| freedict-nl-en | Dutch-English | 22747 | GPL-2.0-or-later |
| freedict-nl-es | Dutch-Spanish | 28097 | CC-BY-SA-3.0 |
| freedict-nl-fi | Dutch-Finnish | 12735 | CC-BY-SA-3.0 |
| freedict-nl-fr | Dutch-French | 16770 | GPL-2.0-or-later |
| freedict-nl-ga | Dutch-Irish | 18643 | CC-BY-SA-3.0 |
| freedict-nl-id | Dutch-Indonesian | 10789 | CC-BY-SA-3.0 |
| freedict-nl-it | Dutch-Italian | 10595 | CC-BY-SA-3.0 |
| freedict-nl-ja | Dutch-Japanese | 10392 | CC-BY-SA-3.0 |
| freedict-nl-la | Dutch-Latin | 12461 | CC-BY-SA-3.0 |
| freedict-nl-lt | Dutch-Lithuanian | 10579 | CC-BY-SA-3.0 |
| freedict-nl-pl | Dutch-Polish | 12201 | CC-BY-SA-3.0 |
| freedict-nl-pt | Dutch-Portuguese | 13233 | CC-BY-SA-3.0 |
| freedict-nl-ru | Dutch-Russian | 13283 | CC-BY-SA-3.0 |
| freedict-nl-sv | Dutch-Swedish | 13342 | CC-BY-SA-3.0 |
| freedict-nn-nb | Norwegian Nynorsk-Norwegian Bokmal | 67987 | GPL-2.0-or-later |
| freedict-oc-ca | Occitan-Catalan | 16679 | GPL-3.0-or-later |
| freedict-pl-bg | Polish-Bulgarian | 14683 | CC-BY-SA-3.0 |
| freedict-pl-ca | Polish-Catalan | 10285 | CC-BY-SA-3.0 |
| freedict-pl-cs | Polish-Czech | 15704 | CC-BY-SA-3.0 |
| freedict-pl-da | Polish-Danish | 14706 | CC-BY-SA-3.0 |
| freedict-pl-de | Polish-German | 26635 | CC-BY-SA-3.0 |
| freedict-pl-el | Polish-Greek | 14283 | CC-BY-SA-3.0 |
| freedict-pl-en | Polish-English | 42088 | CC-BY-SA-3.0 |
| freedict-pl-es | Polish-Spanish | 24058 | CC-BY-SA-3.0 |
| freedict-pl-fi | Polish-Finnish | 19084 | CC-BY-SA-3.0 |
| freedict-pl-fr | Polish-French | 22133 | CC-BY-SA-3.0 |
| freedict-pl-id | Polish-Indonesian | 13556 | CC-BY-SA-3.0 |
| freedict-pl-it | Polish-Italian | 22404 | CC-BY-SA-3.0 |
| freedict-pl-ja | Polish-Japanese | 14401 | CC-BY-SA-3.0 |
| freedict-pl-lt | Polish-Lithuanian | 10824 | CC-BY-SA-3.0 |
| freedict-pl-nl | Polish-Dutch | 17405 | CC-BY-SA-3.0 |
| freedict-pl-no | Polish-Norwegian | 18074 | CC-BY-SA-3.0 |
| freedict-pl-pt | Polish-Portuguese | 11739 | CC-BY-SA-3.0 |
| freedict-pl-ru | Polish-Russian | 28923 | CC-BY-SA-3.0 |
| freedict-pl-sv | Polish-Swedish | 13080 | CC-BY-SA-3.0 |
| freedict-pl-tr | Polish-Turkish | 13246 | CC-BY-SA-3.0 |
| freedict-pt-de | Portuguese-German | 8294 | GPL-2.0-or-later |
| freedict-pt-en | Portuguese-English | 10661 | GPL-2.0-or-later |
| freedict-pt-es | Portuguese-Spanish | 10791 | CC-BY-SA-3.0 |
| freedict-pt-fr | Portuguese-French | 10240 | CC-BY-SA-3.0 |
| freedict-ru-cs | Russian-Czech | 10955 | CC-BY-SA-3.0 |
| freedict-ru-de | Russian-German | 23850 | CC-BY-SA-3.0 |
| freedict-ru-en | Russian-English | 42600 | CC-BY-SA-3.0 |
| freedict-ru-es | Russian-Spanish | 18875 | CC-BY-SA-3.0 |
| freedict-ru-fr | Russian-French | 25142 | CC-BY-SA-3.0 |
| freedict-ru-it | Russian-Italian | 16425 | CC-BY-SA-3.0 |
| freedict-ru-pl | Russian-Polish | 13322 | CC-BY-SA-3.0 |
| freedict-ru-pt | Russian-Portuguese | 10644 | CC-BY-SA-3.0 |
| freedict-ru-sv | Russian-Swedish | 10214 | CC-BY-SA-3.0 |
| freedict-sl-en | Slovenian-English | 5555 | GPL-3.0-or-later |
| freedict-sv-bg | Swedish-Bulgarian | 13646 | CC-BY-SA-3.0 |
| freedict-sv-ca | Swedish-Catalan | 16387 | CC-BY-SA-3.0 |
| freedict-sv-cs | Swedish-Czech | 16261 | CC-BY-SA-3.0 |
| freedict-sv-da | Swedish-Danish | 12714 | CC-BY-SA-3.0 |
| freedict-sv-de | Swedish-German | 44108 | CC-BY-SA-3.0 |
| freedict-sv-el | Swedish-Greek | 16167 | CC-BY-SA-3.0 |
| freedict-sv-en | Swedish-English | 5220 | GPL-2.0-or-later |
| freedict-sv-es | Swedish-Spanish | 15577 | CC-BY-SA-3.0 |
| freedict-sv-fi | Swedish-Finnish | 18714 | CC-BY-SA-3.0 |
| freedict-sv-fr | Swedish-French | 20345 | CC-BY-SA-3.0 |
| freedict-sv-ga | Swedish-Irish | 17065 | CC-BY-SA-3.0 |
| freedict-sv-it | Swedish-Italian | 12985 | CC-BY-SA-3.0 |
| freedict-sv-ja | Swedish-Japanese | 13651 | CC-BY-SA-3.0 |
| freedict-sv-la | Swedish-Latin | 14940 | CC-BY-SA-3.0 |
| freedict-sv-lt | Swedish-Lithuanian | 10671 | CC-BY-SA-3.0 |
| freedict-sv-nl | Swedish-Dutch | 12477 | CC-BY-SA-3.0 |
| freedict-sv-no | Swedish-Norwegian | 17948 | CC-BY-SA-3.0 |
| freedict-sv-pl | Swedish-Polish | 13187 | CC-BY-SA-3.0 |
| freedict-sv-pt | Swedish-Portuguese | 11502 | CC-BY-SA-3.0 |
| freedict-sv-ru | Swedish-Russian | 11851 | CC-BY-SA-3.0 |
| freedict-sv-tr | Swedish-Turkish | 14066 | CC-BY-SA-3.0 |
| freedict-sv-zh | Swedish-Chinese | 11793 | CC-BY-SA-3.0 |
| freedict-sw-en | Swahili-English | 2675 | GPL-2.0-or-later |
| freedict-sw-pl | Swahili-Polish | 1319 | GFDL-1.1-or-later |
| freedict-tr-bg | Turkish-Bulgarian | 10229 | CC-BY-SA-3.0 |
| freedict-tr-ca | Turkish-Catalan | 12202 | CC-BY-SA-3.0 |
| freedict-tr-en | Turkish-English | 1026 | GPL-2.0-or-later |
| freedict-tr-fi | Turkish-Finnish | 14762 | CC-BY-SA-3.0 |
| freedict-tr-ja | Turkish-Japanese | 12012 | CC-BY-SA-3.0 |
| freedict-zh-id | Chinese-Indonesian | 82904 | CC-BY-SA-3.0 |
| freedict-zh-ku | Chinese-Kurdish | 55768 | CC-BY-SA-3.0 |
| freedict-zh-la | Chinese-Latin | 87715 | CC-BY-SA-3.0 |
| freedict-zh-lt | Chinese-Lithuanian | 85992 | CC-BY-SA-3.0 |
| freedict-zh-mg | Chinese-Malagasy | 71285 | CC-BY-SA-3.0 |
| freedict-zh-no | Chinese-Norwegian | 93867 | CC-BY-SA-3.0 |
| freedict-zh-ru | Chinese-Russian | 156590 | CC-BY-SA-3.0 |
