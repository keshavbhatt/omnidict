# Entry HTML contract

The HTML an entry renders to, identical on every client (PLAN D12): the desktop
`EntryRenderer` (`app/src/core/entry_renderer.cpp`), the pipeline's reference renderer
(`pipeline/omnipipe/render.py`), and the web templates of M6. The golden files under
`tests/golden/entries/` are the executable form of this document: each `<name>.json` is an
entry, `<name>.html` the exact bytes every renderer must produce for it. Styling is not part of
the contract; every client brings its own stylesheet for the classes below.

## Input

One entry as in the canonical JSONL record (DOCS/schema.md), which is also what a bundle holds:
`headword`, `lang`, optional `frequency` (1 to 5), `pronunciations` (`ipa`, `region`), `senses`
in display order (`pos`, `pattern`, `label`, `definition`, `examples` with `text` and
`translation`), `forms` (`form`, `tag`) and `relations` (`type`, `target`, optional
`sense_ordinal`, 1-based). A sense's ordinal is its 1-based position.

`definition`, example `text` and `translation` are restricted HTML (PLAN 4.3) and are inserted
as they are. Every other value is plain text and is escaped.

## Escaping

Plain text is escaped by replacing, in this order, `&` with `&amp;`, `<` with `&lt;`, `>` with
`&gt;` and `"` with `&quot;`. Nothing else is escaped (not `'`, not non-ASCII). The same rule
applies inside attribute values.

## Output

Lines are joined with `\n` and the output ends with `\n`. Square brackets below mark an
optional part, `...` a repetition; neither appears in the output. `ESC(x)` is `x` escaped.

```
<div class="entry" lang="ESC(lang)">
<p class="hw-line"><span class="hw">ESC(headword)</span>[ <span class="roman">ESC(roman)</span>][ <span class="freq">STARS</span>]</p>
[<p class="prons">PRON[ · PRON...]</p>]
SENSE...
[ENTRY-RELATION...]
[<p class="forms"><span class="rel-type">Forms</span> FORM[, FORM...]</p>]
</div>
```

- `roman` is the `form` of the entry's first form tagged `romanization`, omitted when none.
- `STARS` is `frequency` times U+25C6 (black diamond) followed by `5 - frequency` times U+25C7
  (white diamond). The span is omitted when `frequency` is absent.
- `PRON` is `[<span class="region">ESC(region)</span> ]<span class="ipa">ESC(ipa)</span>`, for
  each pronunciation with an `ipa`, in order; separator: space, U+00B7 (middle dot), space.
  The paragraph is omitted when there is none.

Each sense, in order:

```
<div class="sense">
<p class="sense-head"><span class="num">ORDINAL</span>[ <span class="pos">POS-NAME</span>][ <span class="pattern">[ESC(pattern)]</span>]</p>
<p class="def">[<span class="label">ESC(label)</span> ]DEFINITION</p>
[<ul class="examples">
<li><span class="ex">TEXT</span>[<br><span class="tr">TRANSLATION</span>]</li>
...
</ul>]
[SENSE-RELATION...]
</div>
```

- The square brackets around the pattern are literal characters in the output.
- `POS-NAME` from `pos`: noun, verb, adjective (adj), adverb (adv), pronoun (pron),
  preposition (prep), conjunction (conj), interjection (interj), determiner (det), numeral
  (num), particle, prefix, suffix, phrase, abbreviation (abbr). The span is omitted for `other`
  and when `pos` is absent.
- An empty string counts as absent for every optional value: `pattern`, `label`,
  `translation`, `frequency` 0, and in the parts below `ipa`, `region` and a form's `tag`.
  (The desktop client's types cannot tell an empty string from a missing one, so the contract
  does not either.)

Relations render as

```
<p class="rel"><span class="rel-type">TYPE-NAME</span> LINK[, LINK...]</p>
```

with `LINK` = `<a href="lex:ESC(target)">ESC(target)</a>`, targets in their stored order.
`TYPE-NAME`: Synonyms (synonym), Antonyms (antonym), See also (see), Derived terms (derived).

- `SENSE-RELATION`: the relations whose `sense_ordinal` is this sense, one paragraph per type
  in the order synonym, antonym, see, derived; a type with no relations is omitted.
- `ENTRY-RELATION`: the relations without a `sense_ordinal` (or with one that matches no
  sense), grouped the same way, after all senses.

`FORM` is `<span class="form">ESC(form)</span>[ <span class="form-tag">(ESC(tag))</span>]` for
the first 12 forms not tagged `romanization`, in order. The paragraph is omitted when there is
none.

## Words shown to users

The type names, "Forms" and the part-of-speech names are the English source strings; the desktop
app translates them with `tr()`, the web with its own catalogue. Golden files use English.
