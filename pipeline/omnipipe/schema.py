"""Canonical data model for Omnidict entries and dictionary metadata.

Frozen dataclasses mirror the JSONL format described in PLAN.md section 4.2.
Validation is strict: unknown keys and out-of-range values raise `SchemaError`
naming the offending field path, so converter bugs are caught early rather
than silently producing a bad bundle.
"""

from __future__ import annotations

import re
from collections.abc import Mapping, Sequence
from dataclasses import dataclass

SCHEMA_VERSION = 3

POS_TAGS: frozenset[str] = frozenset(
    {
        "noun",
        "verb",
        "adj",
        "adv",
        "pron",
        "prep",
        "conj",
        "interj",
        "det",
        "num",
        "particle",
        "prefix",
        "suffix",
        "phrase",
        "abbr",
        "other",
    }
)

RELATION_TYPES: frozenset[str] = frozenset({"synonym", "antonym", "see", "derived"})

_DICT_ID_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)+$")


class SchemaError(ValueError):
    """Raised when JSON input does not conform to the canonical schema."""


def _require_mapping(obj: object, path: str) -> Mapping[str, object]:
    if not isinstance(obj, Mapping):
        raise SchemaError(f"{path}: expected object")
    return obj


def _require_str(
    obj: Mapping[str, object], key: str, path: str, *, allow_empty: bool = False
) -> str:
    if key not in obj:
        raise SchemaError(f"{path}.{key}: required")
    value = obj[key]
    if not isinstance(value, str):
        raise SchemaError(f"{path}.{key}: expected string")
    if not allow_empty and not value.strip():
        raise SchemaError(f"{path}.{key}: required")
    return value


def _optional_str(obj: Mapping[str, object], key: str, path: str) -> str | None:
    if key not in obj or obj[key] is None:
        return None
    value = obj[key]
    if not isinstance(value, str):
        raise SchemaError(f"{path}.{key}: expected string")
    return value


def _require_list(obj: Mapping[str, object], key: str, path: str) -> Sequence[object]:
    if key not in obj:
        raise SchemaError(f"{path}.{key}: required")
    value = obj[key]
    if not isinstance(value, list):
        raise SchemaError(f"{path}.{key}: expected array")
    return value


def _optional_list(obj: Mapping[str, object], key: str, path: str) -> Sequence[object]:
    if key not in obj or obj[key] is None:
        return ()
    value = obj[key]
    if not isinstance(value, list):
        raise SchemaError(f"{path}.{key}: expected array")
    return value


def _check_unknown_keys(obj: Mapping[str, object], allowed: frozenset[str], path: str) -> None:
    unknown = set(obj.keys()) - allowed
    if unknown:
        extra = ", ".join(sorted(unknown))
        raise SchemaError(f"{path}: unknown field(s) {extra}")


@dataclass(frozen=True, slots=True)
class Example:
    """A usage example, optionally translated (bilingual dictionaries)."""

    text: str
    translation: str | None = None

    _FIELDS = frozenset({"text", "translation"})

    @staticmethod
    def from_json(obj: Mapping[str, object], path: str) -> Example:
        _check_unknown_keys(obj, Example._FIELDS, path)
        text = _require_str(obj, "text", path)
        translation = _optional_str(obj, "translation", path)
        return Example(text=text, translation=translation)

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {"text": self.text}
        if self.translation is not None:
            out["translation"] = self.translation
        return out


@dataclass(frozen=True, slots=True)
class Pronunciation:
    """A pronunciation transcription for an entry."""

    ipa: str | None = None
    region: str | None = None
    audio_ref: str | None = None

    _FIELDS = frozenset({"ipa", "region", "audio_ref"})

    @staticmethod
    def from_json(obj: Mapping[str, object], path: str) -> Pronunciation:
        _check_unknown_keys(obj, Pronunciation._FIELDS, path)
        return Pronunciation(
            ipa=_optional_str(obj, "ipa", path),
            region=_optional_str(obj, "region", path),
            audio_ref=_optional_str(obj, "audio_ref", path),
        )

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {}
        if self.ipa is not None:
            out["ipa"] = self.ipa
        if self.region is not None:
            out["region"] = self.region
        if self.audio_ref is not None:
            out["audio_ref"] = self.audio_ref
        return out


@dataclass(frozen=True, slots=True)
class Sense:
    """One meaning of an entry: definition, part of speech, and examples."""

    definition: str
    pos: str | None = None
    pattern: str | None = None
    label: str | None = None
    examples: tuple[Example, ...] = ()

    _FIELDS = frozenset({"definition", "pos", "pattern", "label", "examples"})

    @staticmethod
    def from_json(obj: Mapping[str, object], path: str) -> Sense:
        _check_unknown_keys(obj, Sense._FIELDS, path)
        definition = _require_str(obj, "definition", path)
        pos = _optional_str(obj, "pos", path)
        if pos is not None and pos not in POS_TAGS:
            raise SchemaError(f"{path}.pos: invalid value {pos!r}")
        pattern = _optional_str(obj, "pattern", path)
        label = _optional_str(obj, "label", path)
        raw_examples = _optional_list(obj, "examples", path)
        examples = tuple(
            Example.from_json(
                _require_mapping(item, f"{path}.examples[{i}]"), f"{path}.examples[{i}]"
            )
            for i, item in enumerate(raw_examples)
        )
        return Sense(
            definition=definition, pos=pos, pattern=pattern, label=label, examples=examples
        )

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {"definition": self.definition}
        if self.pos is not None:
            out["pos"] = self.pos
        if self.pattern is not None:
            out["pattern"] = self.pattern
        if self.label is not None:
            out["label"] = self.label
        if self.examples:
            out["examples"] = [e.to_json() for e in self.examples]
        return out


@dataclass(frozen=True, slots=True)
class Form:
    """An inflected or variant spelling of an entry."""

    form: str
    tag: str | None = None

    _FIELDS = frozenset({"form", "tag"})

    @staticmethod
    def from_json(obj: Mapping[str, object], path: str) -> Form:
        _check_unknown_keys(obj, Form._FIELDS, path)
        form = _require_str(obj, "form", path)
        tag = _optional_str(obj, "tag", path)
        return Form(form=form, tag=tag)

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {"form": self.form}
        if self.tag is not None:
            out["tag"] = self.tag
        return out


@dataclass(frozen=True, slots=True)
class Relation:
    """A relation to another headword: synonym, antonym, see-also, derived."""

    type: str
    target: str
    sense_ordinal: int | None = None

    _FIELDS = frozenset({"type", "target", "sense_ordinal"})

    @staticmethod
    def from_json(obj: Mapping[str, object], path: str) -> Relation:
        _check_unknown_keys(obj, Relation._FIELDS, path)
        rel_type = _require_str(obj, "type", path)
        if rel_type not in RELATION_TYPES:
            raise SchemaError(f"{path}.type: invalid value {rel_type!r}")
        target = _require_str(obj, "target", path)
        sense_ordinal: int | None = None
        if "sense_ordinal" in obj and obj["sense_ordinal"] is not None:
            raw = obj["sense_ordinal"]
            if not isinstance(raw, int) or isinstance(raw, bool):
                raise SchemaError(f"{path}.sense_ordinal: expected integer")
            sense_ordinal = raw
        return Relation(type=rel_type, target=target, sense_ordinal=sense_ordinal)

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {"type": self.type, "target": self.target}
        if self.sense_ordinal is not None:
            out["sense_ordinal"] = self.sense_ordinal
        return out


@dataclass(frozen=True, slots=True)
class Entry:
    """One canonical dictionary entry: a headword and everything about it."""

    headword: str
    lang: str
    senses: tuple[Sense, ...]
    frequency: int | None = None
    pronunciations: tuple[Pronunciation, ...] = ()
    forms: tuple[Form, ...] = ()
    relations: tuple[Relation, ...] = ()

    _FIELDS = frozenset(
        {"headword", "lang", "senses", "frequency", "pronunciations", "forms", "relations"}
    )

    @staticmethod
    def from_json(obj: Mapping[str, object]) -> Entry:
        _check_unknown_keys(obj, Entry._FIELDS, "$")
        headword = _require_str(obj, "headword", "$")
        lang = _require_str(obj, "lang", "$")

        raw_senses = _require_list(obj, "senses", "$")
        if not raw_senses:
            raise SchemaError("$.senses: required")
        senses = tuple(
            Sense.from_json(_require_mapping(item, f"$.senses[{i}]"), f"$.senses[{i}]")
            for i, item in enumerate(raw_senses)
        )

        frequency: int | None = None
        if "frequency" in obj and obj["frequency"] is not None:
            raw_freq = obj["frequency"]
            if not isinstance(raw_freq, int) or isinstance(raw_freq, bool):
                raise SchemaError("$.frequency: expected integer")
            if not (1 <= raw_freq <= 5):
                raise SchemaError("$.frequency: must be between 1 and 5")
            frequency = raw_freq

        raw_pron = _optional_list(obj, "pronunciations", "$")
        pronunciations = tuple(
            Pronunciation.from_json(
                _require_mapping(item, f"$.pronunciations[{i}]"), f"$.pronunciations[{i}]"
            )
            for i, item in enumerate(raw_pron)
        )

        raw_forms = _optional_list(obj, "forms", "$")
        forms = tuple(
            Form.from_json(_require_mapping(item, f"$.forms[{i}]"), f"$.forms[{i}]")
            for i, item in enumerate(raw_forms)
        )

        raw_relations = _optional_list(obj, "relations", "$")
        relations = tuple(
            Relation.from_json(_require_mapping(item, f"$.relations[{i}]"), f"$.relations[{i}]")
            for i, item in enumerate(raw_relations)
        )

        return Entry(
            headword=headword,
            lang=lang,
            senses=senses,
            frequency=frequency,
            pronunciations=pronunciations,
            forms=forms,
            relations=relations,
        )

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {"headword": self.headword, "lang": self.lang}
        out["senses"] = [s.to_json() for s in self.senses]
        if self.frequency is not None:
            out["frequency"] = self.frequency
        if self.pronunciations:
            out["pronunciations"] = [p.to_json() for p in self.pronunciations]
        if self.forms:
            out["forms"] = [f.to_json() for f in self.forms]
        if self.relations:
            out["relations"] = [r.to_json() for r in self.relations]
        return out


@dataclass(frozen=True, slots=True)
class DictSpec:
    """Identity and licensing metadata for one dictionary bundle."""

    dict_id: str
    name: str
    source_lang: str
    target_lang: str
    version: str
    publisher: str
    license: str
    license_url: str
    attribution: str
    kind: str
    # Optional (schema_version 3): English language names for codes a client may not
    # know ("Old English" for `ang`), and where the upstream source can be downloaded,
    # which the copyleft licences need offered (DOCS/schema.md).
    source_lang_name: str = ""
    target_lang_name: str = ""
    source_url: str = ""

    _FIELDS = frozenset(
        {
            "dict_id",
            "name",
            "source_lang",
            "target_lang",
            "version",
            "publisher",
            "license",
            "license_url",
            "attribution",
            "kind",
            "source_lang_name",
            "target_lang_name",
            "source_url",
        }
    )

    @staticmethod
    def from_json(obj: Mapping[str, object]) -> DictSpec:
        _check_unknown_keys(obj, DictSpec._FIELDS, "$")
        dict_id = _require_str(obj, "dict_id", "$")
        if not _DICT_ID_RE.match(dict_id):
            raise SchemaError(f"$.dict_id: invalid format {dict_id!r}")
        name = _require_str(obj, "name", "$")
        source_lang = _require_str(obj, "source_lang", "$")
        target_lang = _require_str(obj, "target_lang", "$")
        version = _require_str(obj, "version", "$")
        publisher = _require_str(obj, "publisher", "$")
        license_ = _require_str(obj, "license", "$")
        license_url = _require_str(obj, "license_url", "$")
        attribution = _require_str(obj, "attribution", "$")
        kind = _require_str(obj, "kind", "$")
        if kind not in ("monolingual", "bilingual"):
            raise SchemaError(f"$.kind: invalid value {kind!r}")
        if kind == "monolingual" and source_lang != target_lang:
            raise SchemaError("$.target_lang: must equal source_lang for monolingual dictionaries")
        return DictSpec(
            dict_id=dict_id,
            name=name,
            source_lang=source_lang,
            target_lang=target_lang,
            version=version,
            publisher=publisher,
            license=license_,
            license_url=license_url,
            attribution=attribution,
            kind=kind,
            source_lang_name=_optional_str(obj, "source_lang_name", "$") or "",
            target_lang_name=_optional_str(obj, "target_lang_name", "$") or "",
            source_url=_optional_str(obj, "source_url", "$") or "",
        )

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {
            "dict_id": self.dict_id,
            "name": self.name,
            "source_lang": self.source_lang,
            "target_lang": self.target_lang,
            "version": self.version,
            "publisher": self.publisher,
            "license": self.license,
            "license_url": self.license_url,
            "attribution": self.attribution,
            "kind": self.kind,
        }
        for key, value in (
            ("source_lang_name", self.source_lang_name),
            ("target_lang_name", self.target_lang_name),
            ("source_url", self.source_url),
        ):
            if value:
                out[key] = value
        return out
