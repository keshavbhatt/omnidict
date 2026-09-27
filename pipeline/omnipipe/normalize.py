"""Headword normalization: the Python side of a cross-language contract.

This algorithm must produce byte-identical results to the C++ implementation
in `app/` (which uses ICU4C directly), so `tests/normalize_vectors.json` is
shared between both test suites. Every Unicode-sensitive step goes through
PyICU, never `unicodedata` or `str.casefold`, so both implementations rely on
exactly the same Unicode data tables.

Algorithm (PLAN.md 4.5):
1. NFKC normalize.
2. ICU default full case folding.
3. NFD normalize.
4. Drop non-spacing marks (Unicode general category Mn) that follow a Latin,
   Cyrillic, or Greek base letter (tracked as `base_script`, updated only by
   non-mark code points).
5. NFC normalize.
6. Collapse runs of Unicode whitespace to a single U+0020, then trim.
"""

from __future__ import annotations

from functools import lru_cache

import icu

_LATIN = icu.UScriptCode.LATIN
_CYRILLIC = icu.UScriptCode.CYRILLIC
_GREEK = icu.UScriptCode.GREEK
_STRIPPABLE_SCRIPTS = frozenset({_LATIN, _CYRILLIC, _GREEK})

_NFKC = icu.Normalizer2.getNFKCInstance()
_NFD = icu.Normalizer2.getNFDInstance()
_NFC = icu.Normalizer2.getNFCInstance()


def _nfkc(text: str) -> str:
    return str(_NFKC.normalize(text))


def _nfd(text: str) -> str:
    return str(_NFD.normalize(text))


def _nfc(text: str) -> str:
    return str(_NFC.normalize(text))


def _fold_case(text: str) -> str:
    """ICU default full case folding (U_FOLD_CASE_DEFAULT)."""
    return str(icu.UnicodeString(text).foldCase())


def _is_mark(cp: int) -> bool:
    """True if the code point's general category is Mn, Mc, or Me."""
    category = icu.Char.charType(cp)
    return category in (
        icu.UCharCategory.NON_SPACING_MARK,
        icu.UCharCategory.COMBINING_SPACING_MARK,
        icu.UCharCategory.ENCLOSING_MARK,
    )


def _is_nonspacing_mark(cp: int) -> bool:
    """True if the code point's general category is exactly Mn."""
    return bool(icu.Char.charType(cp) == icu.UCharCategory.NON_SPACING_MARK)


def _script_of(cp: int) -> int:
    return int(icu.Script.getScript(cp).code)


def _strip_diacritics(text: str) -> str:
    """Drop Mn marks that sit on a Latin/Cyrillic/Greek base letter."""
    out: list[str] = []
    base_script = None  # "none" until the first non-mark code point is seen
    for ch in text:
        cp = ord(ch)
        if _is_mark(cp):
            if _is_nonspacing_mark(cp) and base_script in _STRIPPABLE_SCRIPTS:
                continue
            out.append(ch)
            continue
        base_script = _script_of(cp)
        out.append(ch)
    return "".join(out)


def _collapse_whitespace(text: str) -> str:
    """Replace every run of Unicode whitespace with one space, then trim."""
    out: list[str] = []
    in_space = False
    for ch in text:
        if icu.Char.isUWhiteSpace(ord(ch)):
            if not in_space:
                out.append(" ")
            in_space = True
        else:
            out.append(ch)
            in_space = False
    return "".join(out).strip(" ")


def normalize_headword(text: str) -> str:
    """Apply the full headword normalization pipeline defined in PLAN.md 4.5."""
    step = _nfkc(text)
    step = _fold_case(step)
    step = _nfd(step)
    step = _strip_diacritics(step)
    step = _nfc(step)
    return _collapse_whitespace(step)


@lru_cache(maxsize=64)
def _collator_for(locale: str) -> icu.Collator:
    return icu.Collator.createInstance(icu.Locale(locale))


def sort_key(text: str, locale: str) -> bytes:
    """ICU collation key for `text` under `locale`, default strength."""
    collator = _collator_for(locale)
    key: bytes = collator.getSortKey(text)
    return key


def icu_version() -> str:
    """The linked ICU library version, e.g. '78.2'."""
    return str(icu.ICU_VERSION)


def unicode_version() -> str:
    """The Unicode Character Database version ICU was built against."""
    return str(icu.UNICODE_VERSION)
