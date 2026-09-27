from __future__ import annotations

import json
import sqlite3
from collections.abc import Iterator
from pathlib import Path

import pytest

from omnipipe.build import build_bundle, read_jsonl
from omnipipe.normalize import normalize_headword
from omnipipe.schema import DictSpec
from omnipipe.suggest import max_distance, osa_distance, suggest

_CASES_PATH = Path(__file__).resolve().parents[2] / "tests" / "suggest_cases.json"
_CASES = json.loads(_CASES_PATH.read_text(encoding="utf-8"))["cases"]


@pytest.fixture(scope="module")
def fixture_bundle(tmp_path_factory: pytest.TempPathFactory) -> Iterator[sqlite3.Connection]:
    fixtures = Path(__file__).resolve().parents[2] / "tests" / "fixtures"
    spec = DictSpec.from_json(
        json.loads((fixtures / "sample-en.meta.json").read_text(encoding="utf-8"))
    )
    path = build_bundle(
        read_jsonl(fixtures / "sample-en.jsonl"), spec, tmp_path_factory.mktemp("b")
    )
    # Read-only, as the client opens bundles: the vocabulary table lives in temp.
    conn = sqlite3.connect(f"{path.resolve().as_uri()}?mode=ro", uri=True)
    yield conn
    conn.close()


@pytest.mark.parametrize("case", _CASES, ids=[case["query"] for case in _CASES])
def test_shared_cases(fixture_bundle: sqlite3.Connection, case: dict[str, object]) -> None:
    query = normalize_headword(str(case["query"]))
    assert [s.word for s in suggest(fixture_bundle, query)] == case["words"]


def test_suggest_table_holds_each_headword_once_between_markers(
    fixture_bundle: sqlite3.Connection,
) -> None:
    words = [row[0] for row in fixture_bundle.execute("SELECT word FROM suggest ORDER BY rowid")]
    norms = [
        row[0]
        for row in fixture_bundle.execute(
            "SELECT DISTINCT headword_norm FROM entries ORDER BY headword_norm"
        )
    ]
    assert words == ["\u0002" + norm + "\u0003" for norm in norms]


def test_limit_caps_the_list(fixture_bundle: sqlite3.Connection) -> None:
    assert len(suggest(fixture_bundle, "bok", limit=1)) == 1


@pytest.mark.parametrize(
    ("a", "b", "expected"),
    [
        ("perhaps", "perhaps", 0),
        ("prehaps", "perhaps", 1),  # adjacent swap counts once
        ("mouse", "moose", 1),
        ("book", "books", 1),
        ("abc", "ca", 3),  # OSA, not full Damerau: no edit inside a swapped pair
        ("नमसते", "नमस्ते", 1),  # a missing virama is one character
    ],
)
def test_osa_distance(a: str, b: str, expected: int) -> None:
    assert osa_distance(a, b, 5) == expected


def test_osa_distance_stops_past_the_cap() -> None:
    assert osa_distance("dictionary", "xylophone", 2) == 3
    assert osa_distance("a", "abcdef", 2) == 3


def test_max_distance_grows_with_length() -> None:
    assert [max_distance(n) for n in (3, 4, 5, 8, 9, 20)] == [1, 1, 2, 2, 3, 3]
