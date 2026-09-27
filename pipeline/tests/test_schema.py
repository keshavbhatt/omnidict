"""Tests for `omnipipe.schema`."""

from __future__ import annotations

import pytest

from omnipipe.schema import DictSpec, Entry, SchemaError


def test_valid_minimal_entry() -> None:
    obj = {"headword": "run", "lang": "en", "senses": [{"definition": "to move fast"}]}
    entry = Entry.from_json(obj)
    assert entry.headword == "run"
    assert entry.lang == "en"
    assert len(entry.senses) == 1
    assert entry.senses[0].definition == "to move fast"
    assert entry.frequency is None
    assert entry.pronunciations == ()
    assert entry.forms == ()
    assert entry.relations == ()


def test_full_entry_round_trips() -> None:
    obj = {
        "headword": "perhaps",
        "lang": "en",
        "frequency": 3,
        "pronunciations": [{"ipa": "p", "region": "UK", "audio_ref": None}],
        "senses": [
            {
                "pos": "adv",
                "pattern": "ADV with cl/group",
                "label": "vagueness",
                "definition": "<b>perhaps</b>",
                "examples": [{"text": "Perhaps.", "translation": None}],
            }
        ],
        "forms": [{"form": "perhapses", "tag": "plural"}],
        "relations": [{"type": "synonym", "target": "maybe", "sense_ordinal": 1}],
    }
    entry = Entry.from_json(obj)
    round_tripped = Entry.from_json(entry.to_json())
    assert round_tripped == entry


def test_missing_headword_raises_with_field_path() -> None:
    obj = {"lang": "en", "senses": [{"definition": "x"}]}
    with pytest.raises(SchemaError, match=r"\$\.headword: required"):
        Entry.from_json(obj)


def test_empty_headword_raises() -> None:
    obj = {"headword": "  ", "lang": "en", "senses": [{"definition": "x"}]}
    with pytest.raises(SchemaError, match=r"\$\.headword: required"):
        Entry.from_json(obj)


def test_empty_senses_raises() -> None:
    obj = {"headword": "run", "lang": "en", "senses": []}
    with pytest.raises(SchemaError, match=r"\$\.senses: required"):
        Entry.from_json(obj)


def test_missing_sense_definition_raises_with_index() -> None:
    obj = {"headword": "run", "lang": "en", "senses": [{}]}
    with pytest.raises(SchemaError, match=r"\$\.senses\[0\]\.definition: required"):
        Entry.from_json(obj)


def test_invalid_pos_raises() -> None:
    obj = {"headword": "run", "lang": "en", "senses": [{"definition": "x", "pos": "bogus"}]}
    with pytest.raises(SchemaError, match=r"\$\.senses\[0\]\.pos: invalid value"):
        Entry.from_json(obj)


def test_frequency_out_of_range_raises() -> None:
    obj = {"headword": "run", "lang": "en", "senses": [{"definition": "x"}], "frequency": 6}
    with pytest.raises(SchemaError, match=r"\$\.frequency: must be between 1 and 5"):
        Entry.from_json(obj)


def test_frequency_bool_rejected() -> None:
    obj = {"headword": "run", "lang": "en", "senses": [{"definition": "x"}], "frequency": True}
    with pytest.raises(SchemaError, match=r"\$\.frequency: expected integer"):
        Entry.from_json(obj)


def test_unknown_top_level_key_raises() -> None:
    obj = {"headword": "run", "lang": "en", "senses": [{"definition": "x"}], "bogus": 1}
    with pytest.raises(SchemaError, match=r"unknown field"):
        Entry.from_json(obj)


def test_unknown_nested_key_raises() -> None:
    obj = {"headword": "run", "lang": "en", "senses": [{"definition": "x", "bogus": 1}]}
    with pytest.raises(SchemaError, match=r"\$\.senses\[0\]: unknown field"):
        Entry.from_json(obj)


def test_invalid_relation_type_raises() -> None:
    obj = {
        "headword": "run",
        "lang": "en",
        "senses": [{"definition": "x"}],
        "relations": [{"type": "bogus", "target": "y"}],
    }
    with pytest.raises(SchemaError, match=r"\$\.relations\[0\]\.type: invalid value"):
        Entry.from_json(obj)


def test_dict_spec_valid() -> None:
    obj = {
        "dict_id": "sample-en",
        "name": "Sample English",
        "source_lang": "en",
        "target_lang": "en",
        "version": "2026.09.1",
        "publisher": "Omnidict project",
        "license": "GPL-3.0-or-later",
        "license_url": "https://www.gnu.org/licenses/gpl-3.0.html",
        "attribution": "Hand-written sample data",
        "kind": "monolingual",
    }
    spec = DictSpec.from_json(obj)
    assert spec.dict_id == "sample-en"
    assert DictSpec.from_json(spec.to_json()) == spec


def test_dict_spec_bad_dict_id_raises() -> None:
    obj = {
        "dict_id": "SampleEN",
        "name": "Sample English",
        "source_lang": "en",
        "target_lang": "en",
        "version": "1",
        "publisher": "p",
        "license": "l",
        "license_url": "u",
        "attribution": "a",
        "kind": "monolingual",
    }
    with pytest.raises(SchemaError, match=r"\$\.dict_id: invalid format"):
        DictSpec.from_json(obj)


def test_dict_spec_monolingual_lang_mismatch_raises() -> None:
    obj = {
        "dict_id": "sample-en",
        "name": "Sample English",
        "source_lang": "en",
        "target_lang": "fr",
        "version": "1",
        "publisher": "p",
        "license": "l",
        "license_url": "u",
        "attribution": "a",
        "kind": "monolingual",
    }
    with pytest.raises(SchemaError, match=r"\$\.target_lang: must equal source_lang"):
        DictSpec.from_json(obj)
