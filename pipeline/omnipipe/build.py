"""Builds a canonical `dict.sqlite` bundle from a JSONL entry stream.

This is the only place that turns validated `Entry`/`DictSpec` objects into
the on-disk format the Qt client reads (PLAN.md 4.1). It enforces the
pipeline's quality gates: any HTML sanitization warning, empty preview, or
zero-entry input is a hard failure, not a warning, because a broken bundle
should never leave the build.
"""

from __future__ import annotations

import argparse
import json
import logging
import sqlite3
import sys
from collections.abc import Iterable, Iterator, Mapping
from datetime import UTC, datetime
from pathlib import Path
from typing import TextIO

from omnipipe.html_subset import sanitize, to_plain
from omnipipe.normalize import icu_version, normalize_headword, sort_key, unicode_version
from omnipipe.schema import DictSpec, Entry, SchemaError

logger = logging.getLogger(__name__)

_PREVIEW_MAX_CHARS = 160
_PREVIEW_SEARCH_WINDOW = 157
# Rows are written in batches so a dictionary of any size builds in bounded memory.
_BATCH_ENTRIES = 5000
# Keys build_bundle writes itself; extra_meta may not override them.
_RESERVED_META_KEYS = frozenset(
    {
        "dict_id",
        "name",
        "source_lang",
        "target_lang",
        "version",
        "schema_version",
        "publisher",
        "license",
        "license_url",
        "attribution",
        "entry_count",
        "built_at",
        "kind",
        "icu_version",
        "unicode_version",
    }
)

# Kept as one constant so docs and tests can reference the exact DDL (PLAN.md 4.1).
SCHEMA_SQL = """
CREATE TABLE meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE entries (
  id             INTEGER PRIMARY KEY,
  headword       TEXT NOT NULL,
  headword_norm  TEXT NOT NULL,
  lang           TEXT NOT NULL,
  frequency      INTEGER,
  preview        TEXT NOT NULL,
  sort_key       BLOB
);
CREATE INDEX idx_entries_norm ON entries(headword_norm);
CREATE INDEX idx_entries_sort ON entries(sort_key);

CREATE TABLE pronunciations (
  id        INTEGER PRIMARY KEY,
  entry_id  INTEGER NOT NULL REFERENCES entries(id),
  ipa       TEXT,
  region    TEXT,
  audio_ref TEXT
);

CREATE TABLE senses (
  id         INTEGER PRIMARY KEY,
  entry_id   INTEGER NOT NULL REFERENCES entries(id),
  ordinal    INTEGER NOT NULL,
  pos        TEXT,
  pattern    TEXT,
  label      TEXT,
  definition TEXT NOT NULL,
  definition_plain TEXT NOT NULL
);
CREATE INDEX idx_senses_entry ON senses(entry_id, ordinal);

CREATE TABLE examples (
  id          INTEGER PRIMARY KEY,
  sense_id    INTEGER NOT NULL REFERENCES senses(id),
  ordinal     INTEGER NOT NULL,
  text        TEXT NOT NULL,
  translation TEXT
);

CREATE TABLE forms (
  form       TEXT NOT NULL,
  form_norm  TEXT NOT NULL,
  entry_id   INTEGER NOT NULL REFERENCES entries(id),
  tag        TEXT
);
CREATE INDEX idx_forms_norm ON forms(form_norm);

CREATE TABLE relations (
  entry_id   INTEGER NOT NULL REFERENCES entries(id),
  sense_id   INTEGER REFERENCES senses(id),
  type       TEXT NOT NULL,
  target     TEXT NOT NULL
);

CREATE VIRTUAL TABLE fts USING fts5(
  headword, definition_plain, example_text,
  content='',
  tokenize='unicode61 remove_diacritics 2'
);
CREATE TABLE fts_map (rowid INTEGER PRIMARY KEY, entry_id INTEGER NOT NULL);
"""


class BuildError(Exception):
    """Raised when a JSONL entry or the assembled bundle fails a quality gate."""


def _split_schema() -> tuple[str, str]:
    """SCHEMA_SQL as (tables, indexes): indexes are built after the bulk load,
    which is faster and leaves them unfragmented."""
    statements = [part.strip() for part in SCHEMA_SQL.split(";") if part.strip()]
    tables = [stmt for stmt in statements if not stmt.startswith("CREATE INDEX")]
    indexes = [stmt for stmt in statements if stmt.startswith("CREATE INDEX")]
    return ";\n".join(tables) + ";", ";\n".join(indexes) + ";"


def _check_fts5_available() -> None:
    """Probe FTS5 support on a throwaway connection so the real bundle file
    is never touched before `PRAGMA page_size` has had a chance to apply."""
    probe = sqlite3.connect(":memory:")
    try:
        probe.execute("CREATE VIRTUAL TABLE _fts5_probe USING fts5(x)")
    except sqlite3.OperationalError as exc:
        raise BuildError(f"sqlite3 build lacks FTS5 support: {exc}") from exc
    finally:
        probe.close()


def read_jsonl(path: Path) -> Iterator[Entry]:
    """Yield validated `Entry` objects from a canonical JSONL file.

    Any malformed line raises `BuildError` naming the 1-based line number.
    """
    with path.open(encoding="utf-8") as handle:
        yield from _iter_entries(handle, path)


def _iter_entries(handle: TextIO, path: Path) -> Iterator[Entry]:
    for line_number, raw_line in enumerate(handle, start=1):
        line = raw_line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as exc:
            raise BuildError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
        try:
            yield Entry.from_json(obj)
        except SchemaError as exc:
            raise BuildError(f"{path}:{line_number}: {exc}") from exc


def _make_preview(definition_plain: str) -> str:
    text = definition_plain.strip()
    if len(text) <= _PREVIEW_MAX_CHARS:
        return text
    window = text[:_PREVIEW_SEARCH_WINDOW]
    cut = window.rfind(" ")
    truncated = window if cut == -1 else window[:cut]
    return f"{truncated.rstrip()}..."


def _sanitize_or_raise(headword: str, field: str, text: str) -> str:
    result = sanitize(text)
    if result.warnings:
        joined = "; ".join(result.warnings)
        raise BuildError(f"{headword!r}: {field} failed HTML validation: {joined}")
    return result.html


def _utc_now_iso(when: datetime | None) -> str:
    moment = when if when is not None else datetime.now(UTC)
    moment = moment.astimezone(UTC).replace(microsecond=0)
    return moment.strftime("%Y-%m-%dT%H:%M:%SZ")


def build_bundle(
    entries: Iterable[Entry],
    spec: DictSpec,
    out_dir: Path,
    *,
    built_at: datetime | None = None,
    extra_meta: Mapping[str, str] | None = None,
    vacuum: bool = True,
) -> Path:
    """Build `out_dir/dict.sqlite` from `entries`, replacing it atomically.

    `extra_meta` adds `meta` rows beyond the required ones, such as the
    converter and dump date a bundle was made from (`source_converter`,
    `source_dump_date`); it may not override a required key.

    `vacuum=False` skips the final VACUUM, which needs a temporary copy of the
    whole database; a fresh bundle loses little without it.
    """
    extra = dict(extra_meta or {})
    clashing = sorted(_RESERVED_META_KEYS.intersection(extra))
    if clashing:
        raise BuildError(f"extra_meta may not set reserved keys: {', '.join(clashing)}")
    out_dir.mkdir(parents=True, exist_ok=True)
    final_path = out_dir / "dict.sqlite"
    tmp_path = out_dir / "dict.sqlite.tmp"
    if tmp_path.exists():
        tmp_path.unlink()

    _check_fts5_available()
    conn = sqlite3.connect(tmp_path)
    try:
        conn.execute("PRAGMA page_size = 4096")
        conn.execute("PRAGMA journal_mode = OFF")
        tables_sql, indexes_sql = _split_schema()
        conn.executescript(tables_sql)

        entry_count = _write_entries(conn, entries, spec)
        if entry_count == 0:
            raise BuildError("no entries to build: input produced zero entries")
        conn.executescript(indexes_sql)
        conn.execute("INSERT INTO fts (fts) VALUES ('optimize')")

        built_at_iso = _utc_now_iso(built_at)
        meta_rows = [
            ("dict_id", spec.dict_id),
            ("name", spec.name),
            ("source_lang", spec.source_lang),
            ("target_lang", spec.target_lang),
            ("version", spec.version),
            ("schema_version", "1"),
            ("publisher", spec.publisher),
            ("license", spec.license),
            ("license_url", spec.license_url),
            ("attribution", spec.attribution),
            ("entry_count", str(entry_count)),
            ("built_at", built_at_iso),
            ("kind", spec.kind),
            ("icu_version", icu_version()),
            ("unicode_version", unicode_version()),
            *sorted(extra.items()),
        ]
        conn.executemany("INSERT INTO meta (key, value) VALUES (?, ?)", meta_rows)
        conn.commit()

        row = conn.execute("PRAGMA integrity_check").fetchone()
        if row is None or row[0] != "ok":
            raise BuildError(f"integrity_check failed: {row}")
        if vacuum:
            conn.execute("VACUUM")
    except BaseException:
        conn.close()
        tmp_path.unlink(missing_ok=True)
        raise
    conn.close()

    tmp_path.replace(final_path)
    logger.info("built %s with %d entries", final_path, entry_count)
    return final_path


class _Rows:
    """One batch of rows per table, flushed together."""

    def __init__(self) -> None:
        self.clear()

    def clear(self) -> None:
        self.entries: list[tuple[object, ...]] = []
        self.prons: list[tuple[object, ...]] = []
        self.senses: list[tuple[object, ...]] = []
        self.examples: list[tuple[object, ...]] = []
        self.forms: list[tuple[object, ...]] = []
        self.relations: list[tuple[object, ...]] = []
        self.fts: list[tuple[object, ...]] = []
        self.fts_map: list[tuple[object, ...]] = []

    def flush(self, conn: sqlite3.Connection) -> None:
        conn.executemany(
            "INSERT INTO entries (id, headword, headword_norm, lang, frequency, preview, sort_key) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            self.entries,
        )
        conn.executemany(
            "INSERT INTO pronunciations (id, entry_id, ipa, region, audio_ref) "
            "VALUES (?, ?, ?, ?, ?)",
            self.prons,
        )
        conn.executemany(
            "INSERT INTO senses "
            "(id, entry_id, ordinal, pos, pattern, label, definition, definition_plain) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            self.senses,
        )
        conn.executemany(
            "INSERT INTO examples (id, sense_id, ordinal, text, translation) "
            "VALUES (?, ?, ?, ?, ?)",
            self.examples,
        )
        conn.executemany(
            "INSERT INTO forms (form, form_norm, entry_id, tag) VALUES (?, ?, ?, ?)",
            self.forms,
        )
        conn.executemany(
            "INSERT INTO relations (entry_id, sense_id, type, target) VALUES (?, ?, ?, ?)",
            self.relations,
        )
        conn.executemany(
            "INSERT INTO fts (rowid, headword, definition_plain, example_text) VALUES (?, ?, ?, ?)",
            self.fts,
        )
        conn.executemany("INSERT INTO fts_map (rowid, entry_id) VALUES (?, ?)", self.fts_map)
        self.clear()


def _write_entries(conn: sqlite3.Connection, entries: Iterable[Entry], spec: DictSpec) -> int:
    rows = _Rows()

    next_pron_id = 1
    next_sense_id = 1
    next_example_id = 1

    entry_count = 0
    for entry_id, entry in enumerate(entries, start=1):
        entry_count += 1
        headword_norm = normalize_headword(entry.headword)
        if not headword_norm:
            raise BuildError(f"{entry.headword!r}: headword_norm is empty after normalization")
        key = sort_key(entry.headword, spec.source_lang)

        if not entry.senses:
            raise BuildError(f"{entry.headword!r}: entry has no senses")
        first_definition_plain = to_plain(entry.senses[0].definition)
        preview = _make_preview(first_definition_plain)
        if not preview:
            raise BuildError(f"{entry.headword!r}: preview is empty")

        rows.entries.append(
            (entry_id, entry.headword, headword_norm, entry.lang, entry.frequency, preview, key)
        )

        for pron in entry.pronunciations:
            rows.prons.append((next_pron_id, entry_id, pron.ipa, pron.region, pron.audio_ref))
            next_pron_id += 1

        ordinal_to_sense_id: dict[int, int] = {}
        for ordinal, sense in enumerate(entry.senses, start=1):
            sense_id = next_sense_id
            next_sense_id += 1
            ordinal_to_sense_id[ordinal] = sense_id

            definition_html = _sanitize_or_raise(
                entry.headword, f"senses[{ordinal}].definition", sense.definition
            )
            definition_plain = to_plain(definition_html)
            rows.senses.append(
                (
                    sense_id,
                    entry_id,
                    ordinal,
                    sense.pos,
                    sense.pattern,
                    sense.label,
                    definition_html,
                    definition_plain,
                )
            )

            example_plains: list[str] = []
            for ex_ordinal, example in enumerate(sense.examples, start=1):
                text_html = _sanitize_or_raise(
                    entry.headword, f"senses[{ordinal}].examples[{ex_ordinal}].text", example.text
                )
                translation_html = (
                    _sanitize_or_raise(
                        entry.headword,
                        f"senses[{ordinal}].examples[{ex_ordinal}].translation",
                        example.translation,
                    )
                    if example.translation is not None
                    else None
                )
                rows.examples.append(
                    (next_example_id, sense_id, ex_ordinal, text_html, translation_html)
                )
                next_example_id += 1
                example_plains.append(to_plain(text_html))

            rows.fts.append((sense_id, entry.headword, definition_plain, "\n".join(example_plains)))
            rows.fts_map.append((sense_id, entry_id))

        for form in entry.forms:
            form_norm = normalize_headword(form.form)
            rows.forms.append((form.form, form_norm, entry_id, form.tag))

        for relation in entry.relations:
            relation_sense_id = (
                ordinal_to_sense_id.get(relation.sense_ordinal)
                if relation.sense_ordinal is not None
                else None
            )
            rows.relations.append((entry_id, relation_sense_id, relation.type, relation.target))

        if entry_count % _BATCH_ENTRIES == 0:
            rows.flush(conn)

    rows.flush(conn)
    return entry_count


def _entry_count(bundle: Path) -> str:
    conn = sqlite3.connect(bundle)
    try:
        row = conn.execute("SELECT value FROM meta WHERE key = 'entry_count'").fetchone()
    finally:
        conn.close()
    return str(row[0]) if row else "?"


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.build")
    parser.add_argument("--in", dest="in_path", required=True, type=Path)
    parser.add_argument("--meta", dest="meta_path", required=True, type=Path)
    parser.add_argument("--out", dest="out_dir", required=True, type=Path)
    parser.add_argument("--built-at", dest="built_at", default=None)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """CLI entry point: build a bundle from a JSONL file and a meta JSON file."""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(sys.argv[1:] if argv is None else argv)

    try:
        meta_obj = json.loads(args.meta_path.read_text(encoding="utf-8"))
        spec = DictSpec.from_json(meta_obj)
        built_at = (
            datetime.strptime(args.built_at, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)
            if args.built_at
            else None
        )
        out_path = build_bundle(read_jsonl(args.in_path), spec, args.out_dir, built_at=built_at)
    except (BuildError, SchemaError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"built {out_path} with {_entry_count(out_path)} entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
