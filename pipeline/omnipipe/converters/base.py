"""Abstract converter contract (PLAN.md 6.1).

Each dictionary source (Kaikki, FreeDict, WordNet, ...) implements this
interface as its own module under `converters/`. M0 only defines the
contract; source-specific implementations land in M1.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Iterator
from pathlib import Path
from typing import ClassVar

from omnipipe.schema import DictSpec, Entry


class Converter(ABC):
    """Turns one source's dump into canonical `DictSpec`/`Entry` objects."""

    source_id: ClassVar[str]

    @abstractmethod
    def fetch(self, cache_dir: Path) -> Path:
        """Download or refresh the source dump into `cache_dir`, returning its path."""
        raise NotImplementedError

    @abstractmethod
    def dictionaries(self) -> list[DictSpec]:
        """List the dictionaries (dict_ids) this source can emit."""
        raise NotImplementedError

    @abstractmethod
    def iter_records(self, spec: DictSpec) -> Iterator[Entry]:
        """Yield canonical `Entry` objects for `spec` from the fetched dump."""
        raise NotImplementedError
