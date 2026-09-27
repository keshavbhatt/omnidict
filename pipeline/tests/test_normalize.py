"""Tests for `omnipipe.normalize`, driven by the shared cross-language vector file."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest

from omnipipe.normalize import normalize_headword, sort_key


def _load_vectors(repo_root: Path) -> list[dict[str, Any]]:
    path = repo_root / "tests" / "normalize_vectors.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    vectors: list[dict[str, Any]] = data["vectors"]
    return vectors


# Vectors are loaded once at collection time; repo_root here mirrors the
# conftest fixture but must run outside of fixture injection for parametrize.
_REPO_ROOT = Path(__file__).resolve().parents[2]
_VECTORS = _load_vectors(_REPO_ROOT)


@pytest.mark.parametrize(
    ("text", "expected"),
    [(v["input"], v["expected"]) for v in _VECTORS],
    ids=[v["name"] for v in _VECTORS],
)
def test_normalize_vector(text: str, expected: str) -> None:
    assert normalize_headword(text) == expected


@pytest.mark.parametrize(
    "text",
    [v["input"] for v in _VECTORS],
    ids=[v["name"] for v in _VECTORS],
)
def test_normalize_is_idempotent(text: str) -> None:
    once = normalize_headword(text)
    twice = normalize_headword(once)
    assert once == twice


def test_vector_names_are_unique() -> None:
    names = [v["name"] for v in _VECTORS]
    assert len(names) == len(set(names))


def test_sort_key_basic_ordering() -> None:
    assert sort_key("a", "en") < sort_key("b", "en")


def test_sort_key_locale_sensitive_ordering() -> None:
    # Swedish collates "z" before the accented letter "o-umlaut" treats as a
    # separate letter after "z"; German collates it near plain "o".
    assert sort_key("z", "sv") < sort_key("ö", "sv")
    assert sort_key("ö", "de") < sort_key("z", "de")
