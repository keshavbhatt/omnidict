"""Reference implementation of spelling suggestions (ADR-013, DOCS/schema.md "Suggestions").

`suggest` finds the headwords of one bundle closest to a query that matched nothing. The
desktop client's `core::Suggester` (`app/src/core/suggester.cpp`) implements the same steps
and must return the same words; `tests/suggest_cases.json` holds the cases both run.

Steps, on the normalized query:
1. Queries shorter than 3 characters get no suggestions.
2. The query is put between the markers U+0002 and U+0003, as every word in the
   `suggest` table is, and its distinct trigrams (3 consecutive characters) are looked up in the
   `suggest` table's vocabulary; unknown trigrams are dropped. The rest are taken rarest
   first (ties by trigram), at most `MAX_TRIGRAMS`, stopping before their postings together
   would pass `MAX_POSTINGS` (the first is always taken).
3. Every word containing a taken trigram gets one hit per trigram. The `MAX_CANDIDATES`
   words with the most hits (ties by rowid) are candidates.
4. Candidates within `max_distance(len)` optimal-string-alignment edits of the query,
   other than the query itself, are ranked by distance, then length difference, then hits
   (more first), then the word by code point.
"""

from __future__ import annotations

import sqlite3
from collections import Counter
from dataclasses import dataclass
from typing import Final

MIN_QUERY_LENGTH: Final = 3
MAX_TRIGRAMS: Final = 8
MAX_POSTINGS: Final = 100_000
MAX_CANDIDATES: Final = 300
START: Final = "\u0002"
END: Final = "\u0003"


@dataclass(frozen=True)
class Suggestion:
    word: str
    distance: int


def max_distance(length: int) -> int:
    """Edits allowed for a query of `length` characters."""
    if length <= 4:
        return 1
    if length <= 8:
        return 2
    return 3


def osa_distance(a: str, b: str, cap: int) -> int:
    """Optimal string alignment distance between `a` and `b` (insertions, deletions,
    substitutions and swaps of adjacent characters), or `cap + 1` once it exceeds `cap`."""
    if abs(len(a) - len(b)) > cap:
        return cap + 1
    before: list[int] = []
    previous = list(range(len(b) + 1))
    for i in range(1, len(a) + 1):
        current = [i] + [0] * len(b)
        row_min = i
        for j in range(1, len(b) + 1):
            cost = 0 if a[i - 1] == b[j - 1] else 1
            value = min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost)
            if i > 1 and j > 1 and a[i - 1] == b[j - 2] and a[i - 2] == b[j - 1]:
                value = min(value, before[j - 2] + 1)
            current[j] = value
            row_min = min(row_min, value)
        if row_min > cap:
            return cap + 1
        before, previous = previous, current
    return min(previous[-1], cap + 1)


def _trigrams(text: str) -> list[str]:
    return sorted({text[i : i + 3] for i in range(len(text) - 2)})


def _phrase(trigram: str) -> str:
    return '"' + trigram.replace('"', '""') + '"'


def suggest(conn: sqlite3.Connection, query_norm: str, limit: int = 5) -> list[Suggestion]:
    """Up to `limit` suggestions for `query_norm` (already normalized) from the bundle
    open on `conn`."""
    if len(query_norm) < MIN_QUERY_LENGTH:
        return []
    cap = max_distance(len(query_norm))
    conn.execute(
        "CREATE VIRTUAL TABLE IF NOT EXISTS temp.suggest_vocab USING fts5vocab(main, suggest, row)"
    )
    counted: list[tuple[int, str]] = []
    for trigram in _trigrams(START + query_norm + END):
        row = conn.execute(
            "SELECT doc FROM temp.suggest_vocab WHERE term = ?", (trigram,)
        ).fetchone()
        if row is not None and row[0] > 0:
            counted.append((int(row[0]), trigram))
    counted.sort()

    hits: Counter[int] = Counter()
    postings = 0
    for taken, (count, trigram) in enumerate(counted):
        if taken == MAX_TRIGRAMS or (taken > 0 and postings + count > MAX_POSTINGS):
            break
        postings += count
        for (rowid,) in conn.execute(
            "SELECT rowid FROM suggest WHERE suggest MATCH ?", (_phrase(trigram),)
        ):
            hits[rowid] += 1

    candidates = sorted(hits.items(), key=lambda item: (-item[1], item[0]))[:MAX_CANDIDATES]
    ranked: list[tuple[int, int, int, str]] = []
    for rowid, hit_count in candidates:
        row = conn.execute("SELECT word FROM suggest WHERE rowid = ?", (rowid,)).fetchone()
        word = str(row[0])[1:-1]
        if word == query_norm:
            continue
        distance = osa_distance(query_norm, word, cap)
        if distance <= cap:
            ranked.append((distance, abs(len(word) - len(query_norm)), -hit_count, word))
    ranked.sort()
    return [Suggestion(word, distance) for distance, _, _, word in ranked[:limit]]
