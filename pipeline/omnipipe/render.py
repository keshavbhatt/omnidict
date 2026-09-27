"""Reference implementation of the entry HTML contract (DOCS/entry-html.md).

`render_entry` renders one `Entry` (`omnipipe.schema`) to the exact HTML string
every Omnidict client shows for it. The desktop client's `EntryRenderer`
(`app/src/core/entry_renderer.cpp`) and the web templates (M6) must produce
byte-identical output; the golden files under `tests/golden/entries/` are the
executable form of the contract that all three implementations are checked
against.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path

from omnipipe.schema import POS_TAGS, Entry, Example, Form, Pronunciation, Relation, Sense

# English source strings (DOCS/entry-html.md "Words shown to users"): the desktop
# app translates them with `tr()`, the web app with its own catalogue. Golden
# files use English.
_POS_NAMES: Mapping[str, str] = {
    "noun": "noun",
    "verb": "verb",
    "adj": "adjective",
    "adv": "adverb",
    "pron": "pronoun",
    "prep": "preposition",
    "conj": "conjunction",
    "interj": "interjection",
    "det": "determiner",
    "num": "numeral",
    "particle": "particle",
    "prefix": "prefix",
    "suffix": "suffix",
    "phrase": "phrase",
    "abbr": "abbreviation",
}
# `other` has no span at all (DOCS/entry-html.md); every other tag in the fixed
# POS set must have a display name.
assert set(_POS_NAMES) == POS_TAGS - {"other"}

_RELATION_TYPE_NAMES: Mapping[str, str] = {
    "synonym": "Synonyms",
    "antonym": "Antonyms",
    "see": "See also",
    "derived": "Derived terms",
}
# Display order for grouped relation paragraphs, sense-level and entry-level alike.
_RELATION_ORDER: tuple[str, ...] = ("synonym", "antonym", "see", "derived")

_FORMS_LABEL = "Forms"

_ROMANIZATION_TAG = "romanization"
_MAX_FORMS_SHOWN = 12

_STAR_FILLED = "◆"  # black diamond
_STAR_EMPTY = "◇"  # white diamond
_MIDDLE_DOT = "·"


def _esc(text: str) -> str:
    """Escape plain text per DOCS/entry-html.md: only `& < > "`, in that order."""
    return (
        text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
    )


def _stars(frequency: int) -> str:
    return _STAR_FILLED * frequency + _STAR_EMPTY * (5 - frequency)


def _first_romanization(forms: Sequence[Form]) -> str | None:
    for form in forms:
        if form.tag == _ROMANIZATION_TAG:
            return form.form
    return None


def _render_hw_line(entry: Entry) -> str:
    parts = [f'<span class="hw">{_esc(entry.headword)}</span>']
    roman = _first_romanization(entry.forms)
    if roman is not None:
        parts.append(f'<span class="roman">{_esc(roman)}</span>')
    if entry.frequency:
        parts.append(f'<span class="freq">{_stars(entry.frequency)}</span>')
    return f'<p class="hw-line">{" ".join(parts)}</p>'


def _render_prons(pronunciations: Sequence[Pronunciation]) -> str | None:
    pieces: list[str] = []
    for pron in pronunciations:
        if not pron.ipa:
            continue
        piece = ""
        if pron.region:
            piece += f'<span class="region">{_esc(pron.region)}</span> '
        piece += f'<span class="ipa">{_esc(pron.ipa)}</span>'
        pieces.append(piece)
    if not pieces:
        return None
    separator = f" {_MIDDLE_DOT} "
    return f'<p class="prons">{separator.join(pieces)}</p>'


def _render_sense_head(ordinal: int, sense: Sense) -> str:
    parts = [f'<span class="num">{ordinal}</span>']
    pos_name = _POS_NAMES.get(sense.pos) if sense.pos is not None else None
    if pos_name is not None:
        parts.append(f'<span class="pos">{pos_name}</span>')
    if sense.pattern:
        parts.append(f'<span class="pattern">[{_esc(sense.pattern)}]</span>')
    return f'<p class="sense-head">{" ".join(parts)}</p>'


def _render_def(sense: Sense) -> str:
    label_prefix = f'<span class="label">{_esc(sense.label)}</span> ' if sense.label else ""
    return f'<p class="def">{label_prefix}{sense.definition}</p>'


def _render_examples(examples: Sequence[Example]) -> list[str]:
    if not examples:
        return []
    lines = ['<ul class="examples">']
    for example in examples:
        tr = f'<br><span class="tr">{example.translation}</span>' if example.translation else ""
        lines.append(f'<li><span class="ex">{example.text}</span>{tr}</li>')
    lines.append("</ul>")
    return lines


def _group_relations(relations: Sequence[Relation]) -> dict[str, list[Relation]]:
    grouped: dict[str, list[Relation]] = {rel_type: [] for rel_type in _RELATION_ORDER}
    for relation in relations:
        grouped[relation.type].append(relation)
    return grouped


def _render_relation_paragraphs(relations: Sequence[Relation]) -> list[str]:
    grouped = _group_relations(relations)
    lines: list[str] = []
    for rel_type in _RELATION_ORDER:
        group = grouped[rel_type]
        if not group:
            continue
        links = ", ".join(
            f'<a href="lex:{_esc(rel.target)}">{_esc(rel.target)}</a>' for rel in group
        )
        type_name = _RELATION_TYPE_NAMES[rel_type]
        lines.append(f'<p class="rel"><span class="rel-type">{type_name}</span> {links}</p>')
    return lines


def _sense_relations(relations: Sequence[Relation], ordinal: int) -> list[Relation]:
    return [rel for rel in relations if rel.sense_ordinal == ordinal]


def _entry_relations(relations: Sequence[Relation], sense_count: int) -> list[Relation]:
    return [
        rel
        for rel in relations
        if rel.sense_ordinal is None or not (1 <= rel.sense_ordinal <= sense_count)
    ]


def _render_sense(sense: Sense, ordinal: int, relations: Sequence[Relation]) -> list[str]:
    lines = ['<div class="sense">']
    lines.append(_render_sense_head(ordinal, sense))
    lines.append(_render_def(sense))
    lines.extend(_render_examples(sense.examples))
    lines.extend(_render_relation_paragraphs(_sense_relations(relations, ordinal)))
    lines.append("</div>")
    return lines


def _render_forms(forms: Sequence[Form]) -> str | None:
    shown = [form for form in forms if form.tag != _ROMANIZATION_TAG][:_MAX_FORMS_SHOWN]
    if not shown:
        return None
    pieces: list[str] = []
    for form in shown:
        piece = f'<span class="form">{_esc(form.form)}</span>'
        if form.tag:
            piece += f' <span class="form-tag">({_esc(form.tag)})</span>'
        pieces.append(piece)
    return f'<p class="forms"><span class="rel-type">{_FORMS_LABEL}</span> {", ".join(pieces)}</p>'


def render_entry(entry: Entry) -> str:
    """Render `entry` to the exact HTML string defined by DOCS/entry-html.md."""
    lines: list[str] = [f'<div class="entry" lang="{_esc(entry.lang)}">']
    lines.append(_render_hw_line(entry))

    prons_line = _render_prons(entry.pronunciations)
    if prons_line is not None:
        lines.append(prons_line)

    for ordinal, sense in enumerate(entry.senses, start=1):
        lines.extend(_render_sense(sense, ordinal, entry.relations))

    lines.extend(_render_relation_paragraphs(_entry_relations(entry.relations, len(entry.senses))))

    forms_line = _render_forms(entry.forms)
    if forms_line is not None:
        lines.append(forms_line)

    lines.append("</div>")
    return "\n".join(lines) + "\n"


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.render")
    parser.add_argument(
        "path",
        type=Path,
        help="an entry JSON file, or (with --write) a directory of them",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="rewrite <name>.html next to every <name>.json in the given directory",
    )
    return parser.parse_args(argv)


def _load_entry(json_path: Path) -> Entry:
    obj = json.loads(json_path.read_text(encoding="utf-8"))
    return Entry.from_json(obj)


def main(argv: list[str] | None = None) -> int:
    """CLI entry point: render one entry to stdout, or rewrite a directory of goldens."""
    args = _parse_args(sys.argv[1:] if argv is None else argv)

    if args.write:
        for json_path in sorted(args.path.glob("*.json")):
            html = render_entry(_load_entry(json_path))
            json_path.with_suffix(".html").write_text(html, encoding="utf-8")
        return 0

    sys.stdout.write(render_entry(_load_entry(args.path)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
