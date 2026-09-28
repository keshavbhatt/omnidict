"""Kengdic, Joe Speigle's Korean-English dictionary database, PLAN.md 6.2.

Kengdic is kept on GitHub as one tab-separated file, `kengdic.tsv`, with a
header row and one row per (Korean word, English gloss):

    id  surface  hanja  gloss  level  created  source

Mapping decisions (DOCS/sources.md has the long version):
- every row with the same surface (whitespace collapsed) merges into one
  entry, one sense per gloss in `id` order; a gloss repeated word for word
  (ignoring case and spacing) is kept once;
- rows without a gloss add nothing but their hanja: an entry needs at least
  one glossed row;
- the hanja column (comma-separated when a word has several spellings)
  becomes `forms` tagged `hanja`, so a query in hanja finds the entry;
- the level column (A, B, C: the basic vocabulary grades of the word list
  Kengdic merged in) becomes the frequency band 5, 4, 3; D and blank are
  left unset. A graded row's gloss is the basic meaning, so graded senses
  come first (A, then B, then C), the rest keep row order;
- part of speech is not given by Kengdic and is left `null`.

The fetch pins the last commit that changed `kengdic.tsv` and downloads the
file at that commit, so a bundle records exactly which source it came from.

Run: `python -m omnipipe.converters.kengdic --dict kengdic-ko-en --out out/`
"""

from __future__ import annotations

import argparse
import csv
import html
import io
import json
import logging
import re
import shutil
import sys
import urllib.error
import urllib.request
from collections.abc import Iterable, Iterator, Mapping
from dataclasses import dataclass, field
from datetime import UTC, date, datetime
from itertools import islice
from pathlib import Path
from typing import BinaryIO, ClassVar

from omnipipe.build import BuildError, build_bundle
from omnipipe.converters.base import Converter
from omnipipe.schema import DictSpec, Entry, Form, SchemaError, Sense

logger = logging.getLogger(__name__)

REPOSITORY = "garfieldnate/kengdic"
DATA_FILE = "kengdic.tsv"
COMMITS_URL = f"https://api.github.com/repos/{REPOSITORY}/commits?path={DATA_FILE}&per_page=1"
USER_AGENT = "omnipipe/0.1 (+https://github.com/keshavbhatt/omnidict)"

_DICT_ID = "kengdic-ko-en"
_NAME = "Korean-English (Kengdic)"
_PUBLISHER = "Joe Speigle and Kengdic contributors"
# Kengdic's README offers a choice of MPL 2.0 or LGPL 2.0 or later (DOCS/sources.md).
_LICENSE = "MPL-2.0 OR LGPL-2.0-or-later"
_LICENSE_URL = "https://www.mozilla.org/MPL/2.0/"
_ATTRIBUTION = (
    "Data from Kengdic (https://github.com/garfieldnate/kengdic), created by Joe Speigle, "
    "© Kengdic contributors, MPL 2.0 or LGPL 2.0 or later"
)

_COLUMNS = ("id", "surface", "hanja", "gloss", "level", "created", "source")
_LEVEL_FREQUENCY: Mapping[str, int] = {"A": 5, "B": 4, "C": 3}
_SPACE_RE = re.compile(r"\s+")


class KengdicError(Exception):
    """Raised when the Kengdic file cannot be fetched, read, or matched."""


# ---------------------------------------------------------------------------
# Fetch: the data file at the last commit that changed it.
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class DumpInfo:
    """What was fetched, kept next to the file as `<file>.json`."""

    url: str
    commit: str
    commit_date: date
    fetched_at: str

    def to_json(self) -> dict[str, object]:
        return {
            "url": self.url,
            "commit": self.commit,
            "commit_date": self.commit_date.isoformat(),
            "fetched_at": self.fetched_at,
        }

    @staticmethod
    def from_json(obj: Mapping[str, object]) -> DumpInfo:
        return DumpInfo(
            url=str(obj["url"]),
            commit=str(obj["commit"]),
            commit_date=date.fromisoformat(str(obj["commit_date"])),
            fetched_at=str(obj["fetched_at"]),
        )


def raw_url(commit: str) -> str:
    return f"https://raw.githubusercontent.com/{REPOSITORY}/{commit}/{DATA_FILE}"


def dump_path(cache_dir: Path) -> Path:
    return cache_dir / DATA_FILE


def read_dump_info(dump: Path) -> DumpInfo:
    info_path = dump.with_name(dump.name + ".json")
    if not info_path.exists():
        raise KengdicError(f"{info_path} is missing: fetch the file first")
    obj = json.loads(info_path.read_text(encoding="utf-8"))
    if not isinstance(obj, Mapping):
        raise KengdicError(f"{info_path} is not a JSON object")
    return DumpInfo.from_json(obj)


def _download(url: str, out: BinaryIO, timeout: float) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            shutil.copyfileobj(response, out, 1 << 20)
    except urllib.error.HTTPError as exc:
        raise KengdicError(f"cannot fetch {url}: HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise KengdicError(f"cannot fetch {url}: {exc.reason}") from exc


def latest_commit(*, timeout: float = 60.0) -> tuple[str, date]:
    """The last commit that changed the data file, and its date."""
    reply = io.BytesIO()
    _download(COMMITS_URL, reply, timeout)
    try:
        commits = json.loads(reply.getvalue().decode("utf-8"))
        newest = commits[0]
        sha = str(newest["sha"])
        stamp = str(newest["commit"]["committer"]["date"])
    except (ValueError, IndexError, KeyError, TypeError) as exc:
        raise KengdicError(f"{COMMITS_URL}: unexpected reply") from exc
    return sha, datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC).date()


def fetch_dump(cache_dir: Path, *, timeout: float = 120.0) -> Path:
    """Download the data file into `cache_dir` unless the copy there is that commit's."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    target = dump_path(cache_dir)
    info_path = target.with_name(target.name + ".json")
    commit, commit_date = latest_commit(timeout=timeout)
    if target.exists() and info_path.exists() and read_dump_info(target).commit == commit:
        logger.info("%s is current (%s)", target, commit)
        return target

    url = raw_url(commit)
    partial = target.with_name(target.name + ".part")
    try:
        with partial.open("wb") as out:
            _download(url, out, timeout)
    except KengdicError:
        partial.unlink(missing_ok=True)
        raise
    partial.replace(target)
    fetched_at = datetime.now(UTC).replace(microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")
    info = DumpInfo(url, commit, commit_date, fetched_at)
    info_path.write_text(json.dumps(info.to_json(), indent=2) + "\n", encoding="utf-8")
    logger.info("fetched %s at %s", target, commit)
    return target


def bundle_version(commit_date: date, build: int = 1) -> str:
    """`YYYY.MM.N`: the month of the data file's last change and a rebuild counter."""
    return f"{commit_date.year}.{commit_date.month:02d}.{build}"


def spec_for(version: str, commit: str = "") -> DictSpec:
    """The dictionary's spec; `commit` pins the source link to the file that was built."""
    return DictSpec(
        dict_id=_DICT_ID,
        name=_NAME,
        source_lang="ko",
        target_lang="en",
        version=version,
        publisher=_PUBLISHER,
        license=_LICENSE,
        license_url=_LICENSE_URL,
        attribution=_ATTRIBUTION,
        kind="bilingual",
        source_lang_name="Korean",
        target_lang_name="English",
        source_url=raw_url(commit) if commit else f"https://github.com/{REPOSITORY}",
    )


# ---------------------------------------------------------------------------
# Rows and conversion.
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Row:
    surface: str
    hanja: tuple[str, ...]
    gloss: str
    level: str


def _clean(text: str | None) -> str:
    return _SPACE_RE.sub(" ", text or "").strip()


def parse_row(fields: Mapping[str, str | None]) -> Row | None:
    """One TSV row as a `Row`, or `None` when it has no Korean surface."""
    surface = _clean(fields.get("surface"))
    if not surface:
        return None
    hanja = tuple(h for h in (_clean(p) for p in (fields.get("hanja") or "").split(",")) if h)
    return Row(
        surface=surface,
        hanja=hanja,
        gloss=_clean(fields.get("gloss")),
        level=_clean(fields.get("level")),
    )


def read_rows(dump: Path) -> Iterator[Mapping[str, str | None]]:
    """The data file's rows as column -> value, header checked."""
    with dump.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t", quoting=csv.QUOTE_NONE)
        if tuple(reader.fieldnames or ()) != _COLUMNS:
            raise KengdicError(f"{dump}: unexpected header {reader.fieldnames}")
        yield from reader


@dataclass(slots=True)
class ConversionStats:
    rows: int = 0
    entries: int = 0
    no_surface: int = 0
    no_gloss: int = 0
    repeated: int = 0


@dataclass(slots=True)
class _Accumulator:
    """The entry being assembled from every row seen for one surface."""

    surface: str
    senses: list[tuple[int, Sense]] = field(default_factory=list)  # (sort rank, sense)
    forms: list[Form] = field(default_factory=list)
    frequency: int | None = None
    _glosses: set[str] = field(default_factory=set)

    def add(self, row: Row, stats: ConversionStats) -> None:
        for spelling in row.hanja:
            if spelling != self.surface and all(f.form != spelling for f in self.forms):
                self.forms.append(Form(form=spelling, tag="hanja"))
        band = _LEVEL_FREQUENCY.get(row.level)
        if band is not None and (self.frequency is None or band > self.frequency):
            self.frequency = band
        if not row.gloss:
            stats.no_gloss += 1
            return
        key = row.gloss.casefold()
        if key in self._glosses:
            stats.repeated += 1
            return
        self._glosses.add(key)
        rank = -(band or 0)
        self.senses.append((rank, Sense(definition=html.escape(row.gloss, quote=False))))

    def entry(self) -> Entry | None:
        if not self.senses:
            return None
        return Entry(
            headword=self.surface,
            lang="ko",
            senses=tuple(sense for _, sense in sorted(self.senses, key=lambda pair: pair[0])),
            forms=tuple(self.forms),
            frequency=self.frequency,
        )


def convert_rows(
    rows: Iterable[Mapping[str, str | None]], stats: ConversionStats | None = None
) -> Iterator[Entry]:
    """Canonical entries from Kengdic rows, one per surface, in order of first row."""
    stats = stats if stats is not None else ConversionStats()
    accumulators: dict[str, _Accumulator] = {}
    for fields in rows:
        stats.rows += 1
        row = parse_row(fields)
        if row is None:
            stats.no_surface += 1
            continue
        accumulators.setdefault(row.surface, _Accumulator(surface=row.surface)).add(row, stats)
    for accumulator in accumulators.values():
        entry = accumulator.entry()
        if entry is not None:
            stats.entries += 1
            yield entry


class KengdicConverter(Converter):
    """The `Converter` contract over one cached Kengdic data file."""

    source_id: ClassVar[str] = "kengdic"

    def __init__(self, cache_dir: Path) -> None:
        self.cache_dir = cache_dir
        self.stats = ConversionStats()

    def fetch(self, cache_dir: Path) -> Path:
        return fetch_dump(cache_dir)

    def dictionaries(self) -> list[DictSpec]:
        dump = dump_path(self.cache_dir)
        if not dump.exists():
            return []
        info = read_dump_info(dump)
        return [spec_for(bundle_version(info.commit_date), info.commit)]

    def iter_records(self, spec: DictSpec) -> Iterator[Entry]:
        if spec.dict_id != _DICT_ID:
            raise KengdicError(
                f"unknown dictionary {spec.dict_id!r}; kengdic only builds {_DICT_ID!r}"
            )
        return convert_rows(read_rows(dump_path(self.cache_dir)), stats=self.stats)


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m omnipipe.converters.kengdic")
    parser.add_argument("--dict", dest="dict_id", help=f"dictionary to build ({_DICT_ID})")
    parser.add_argument(
        "--out", type=Path, default=Path("out"), help="bundles go to OUT/<dict_id>/"
    )
    parser.add_argument("--cache", type=Path, default=Path("sources/kengdic"))
    parser.add_argument("--no-fetch", action="store_true", help="use the cached file as is")
    parser.add_argument("--fetch-only", action="store_true", help="refresh the file, build nothing")
    parser.add_argument("--limit", type=int, default=None, help="stop after N entries (testing)")
    parser.add_argument("--build-number", type=int, default=1, help="N in version YYYY.MM.N")
    parser.add_argument("--built-at", default=None, help="fixed build time, YYYY-MM-DDTHH:MM:SSZ")
    parser.add_argument(
        "--no-vacuum",
        action="store_true",
        help="skip the final VACUUM (it needs a temporary copy of the whole bundle)",
    )
    parser.add_argument("--list", action="store_true", help="list the dictionaries and exit")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """CLI: fetch the Kengdic file if needed, convert it, build its bundle."""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    if args.list:
        print(f"{_DICT_ID}\t{_NAME}\thttps://github.com/{REPOSITORY}")
        return 0
    if args.dict_id != _DICT_ID:
        print(f"error: --dict must be {_DICT_ID!r} (see --list)", file=sys.stderr)
        return 1

    try:
        dump = dump_path(args.cache)
        if not (args.no_fetch and dump.exists()):
            fetch_dump(args.cache)
        if args.fetch_only:
            return 0
        info = read_dump_info(dump)
        spec = spec_for(bundle_version(info.commit_date, args.build_number), info.commit)
        converter = KengdicConverter(args.cache)
        entries: Iterable[Entry] = converter.iter_records(spec)
        if args.limit is not None:
            entries = islice(entries, args.limit)
        built_at = (
            datetime.strptime(args.built_at, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)
            if args.built_at
            else None
        )
        out_path = build_bundle(
            entries,
            spec,
            args.out / spec.dict_id,
            built_at=built_at,
            extra_meta={
                "source_converter": KengdicConverter.source_id,
                "source_dump_date": info.commit_date.isoformat(),
            },
            vacuum=not args.no_vacuum,
        )
    except (KengdicError, BuildError, SchemaError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    stats = converter.stats
    print(
        f"built {out_path}: {stats.entries} entries from {stats.rows} rows "
        f"({stats.no_gloss} without a gloss, {stats.repeated} repeated glosses, "
        f"{stats.no_surface} without a Korean word)"
    )
    return 0


__all__ = [
    "COMMITS_URL",
    "ConversionStats",
    "DumpInfo",
    "KengdicConverter",
    "KengdicError",
    "Row",
    "bundle_version",
    "convert_rows",
    "fetch_dump",
    "latest_commit",
    "parse_row",
    "raw_url",
    "read_dump_info",
    "read_rows",
    "spec_for",
]

if __name__ == "__main__":
    raise SystemExit(main())
