"""Shared pytest fixtures for the omnipipe test suite."""

from __future__ import annotations

from pathlib import Path

import pytest


@pytest.fixture
def repo_root() -> Path:
    """The omnidict repository root, resolved relative to this file."""
    return Path(__file__).resolve().parents[2]


@pytest.fixture
def fixtures_dir(repo_root: Path) -> Path:
    return repo_root / "tests" / "fixtures"
