"""Tests for `omnipipe.converters.oewn`.

The WN-LMF sample below is hand-written in the shape of the real Open English
WordNet dump (LexicalEntry/Lemma/Sense, Synset/Definition/Example/SynsetRelation);
no WordNet text is copied.
"""

from __future__ import annotations

import gzip
import io
import json
import sqlite3
import urllib.error
import urllib.request
from email.message import Message
from pathlib import Path

import pytest

from omnipipe.build import build_bundle
from omnipipe.converters import oewn
from omnipipe.converters.oewn import (
    DICT_ID,
    OewnConverter,
    OewnError,
    bundle_version,
    convert_dump,
    fetch_dump,
    parse_dump,
    read_dump_info,
    spec_for,
)
from omnipipe.html_subset import validate
from omnipipe.schema import Entry

_SAMPLE = """<?xml version="1.0" encoding="UTF-8"?>
<LexicalResource>
  <Lexicon id="test" label="Test" language="en" email="x@example.invalid"
           license="https://creativecommons.org/licenses/by/4.0" version="1">
    <LexicalEntry id="e-run-n">
      <Lemma writtenForm="run" partOfSpeech="n">
        <Pronunciation variety="GB">&#633;&#652;n</Pronunciation>
      </Lemma>
      <Sense id="s-run-n-01" synset="syn-run-n"/>
    </LexicalEntry>
    <LexicalEntry id="e-jog-n">
      <Lemma writtenForm="jog" partOfSpeech="n"/>
      <Sense id="s-jog-n-01" synset="syn-run-n"/>
    </LexicalEntry>
    <LexicalEntry id="e-run-v">
      <Lemma writtenForm="run" partOfSpeech="v"/>
      <Form writtenForm="ran"/>
      <Sense id="s-run-v-01" synset="syn-run-v">
        <SenseRelation relType="antonym" target="s-walk-v-01"/>
      </Sense>
    </LexicalEntry>
    <LexicalEntry id="e-walk-v">
      <Lemma writtenForm="walk" partOfSpeech="v"/>
      <Sense id="s-walk-v-01" synset="syn-walk-v"/>
    </LexicalEntry>
    <LexicalEntry id="e-move-v">
      <Lemma writtenForm="move" partOfSpeech="v"/>
      <Sense id="s-move-v-01" synset="syn-move-v"/>
    </LexicalEntry>
    <Synset id="syn-run-n" partOfSpeech="n" members="e-run-n e-jog-n">
      <Definition>an act of running at a moderate pace</Definition>
      <Example>she went for a run</Example>
      <Example>a five mile run</Example>
      <Example>a run before breakfast</Example>
      <Example>an early morning run</Example>
    </Synset>
    <Synset id="syn-run-v" partOfSpeech="v" members="e-run-v">
      <Definition>move fast by using one's feet</Definition>
      <Example>he ran to the store</Example>
      <SynsetRelation relType="hypernym" target="syn-move-v"/>
    </Synset>
    <Synset id="syn-walk-v" partOfSpeech="v" members="e-walk-v">
      <Definition>use one's feet to advance at a moderate pace</Definition>
    </Synset>
    <Synset id="syn-move-v" partOfSpeech="v" members="e-move-v">
      <Definition>change location</Definition>
    </Synset>
  </Lexicon>
</LexicalResource>
"""


def write_sample(path: Path, text: str = _SAMPLE) -> Path:
    path.write_text(text, encoding="utf-8")
    return path


def convert(path: Path) -> dict[str, Entry]:
    parsed = parse_dump(path)
    return {e.headword: e for e in convert_dump(parsed)}


# --- mapping -----------------------------------------------------------------


def test_lexical_entries_sharing_a_written_form_merge_across_pos(tmp_path: Path) -> None:
    entries = convert(write_sample(tmp_path / "sample.xml"))
    run = entries["run"]
    assert [s.pos for s in run.senses] == ["noun", "verb"]


def test_definition_and_examples_from_synset(tmp_path: Path) -> None:
    entries = convert(write_sample(tmp_path / "sample.xml"))
    run = entries["run"]
    noun_sense, verb_sense = run.senses
    assert noun_sense.definition == "an act of running at a moderate pace"
    assert [e.text for e in noun_sense.examples] == [
        "she went for a run",
        "a five mile run",
        "a run before breakfast",
    ]
    assert verb_sense.definition == "move fast by using one's feet"
    assert [e.text for e in verb_sense.examples] == ["he ran to the store"]


def test_example_count_is_capped_at_three(tmp_path: Path) -> None:
    entries = convert(write_sample(tmp_path / "sample.xml"))
    assert len(entries["run"].senses[0].examples) == 3


def test_synonyms_from_synset_members_exclude_the_headword(tmp_path: Path) -> None:
    entries = convert(write_sample(tmp_path / "sample.xml"))
    run = entries["run"]
    synonyms = [r for r in run.relations if r.type == "synonym"]
    assert [(r.target, r.sense_ordinal) for r in synonyms] == [("jog", 1)]


def test_antonym_from_sense_relation(tmp_path: Path) -> None:
    entries = convert(write_sample(tmp_path / "sample.xml"))
    run = entries["run"]
    antonyms = [r for r in run.relations if r.type == "antonym"]
    assert [(r.target, r.sense_ordinal) for r in antonyms] == [("walk", 2)]


def test_hypernym_becomes_a_see_relation(tmp_path: Path) -> None:
    entries = convert(write_sample(tmp_path / "sample.xml"))
    run = entries["run"]
    see = [r for r in run.relations if r.type == "see"]
    assert [(r.target, r.sense_ordinal) for r in see] == [("move", 2)]


def test_forms_are_tagged_inflection_and_exclude_the_headword(tmp_path: Path) -> None:
    entries = convert(write_sample(tmp_path / "sample.xml"))
    run = entries["run"]
    assert [(f.form, f.tag) for f in run.forms] == [("ran", "inflection")]


def test_pronunciation_variety_gb_maps_to_region_uk(tmp_path: Path) -> None:
    entries = convert(write_sample(tmp_path / "sample.xml"))
    run = entries["run"]
    assert [(p.ipa, p.region) for p in run.pronunciations] == [("ɹʌn", "UK")]


def test_entries_without_a_definition_are_skipped(tmp_path: Path) -> None:
    sample = _SAMPLE.replace(
        '<Synset id="syn-walk-v" partOfSpeech="v" members="e-walk-v">\n'
        "      <Definition>use one's feet to advance at a moderate pace</Definition>\n"
        "    </Synset>",
        '<Synset id="syn-walk-v" partOfSpeech="v" members="e-walk-v"></Synset>',
    )
    entries = convert(write_sample(tmp_path / "sample.xml", sample))
    assert "walk" not in entries


def test_synonym_count_is_capped_at_ten(tmp_path: Path) -> None:
    members = " ".join(f"e-w{i}" for i in range(12))
    entries_xml = "\n".join(
        f'<LexicalEntry id="e-w{i}"><Lemma writtenForm="w{i}" partOfSpeech="n"/>'
        f'<Sense id="s-w{i}" synset="syn-many"/></LexicalEntry>'
        for i in range(12)
    )
    sample = f"""<?xml version="1.0" encoding="UTF-8"?>
<LexicalResource><Lexicon id="test" label="Test" language="en" email="x@example.invalid"
license="https://creativecommons.org/licenses/by/4.0" version="1">
{entries_xml}
<Synset id="syn-many" partOfSpeech="n" members="{members}">
<Definition>one of many</Definition>
</Synset>
</Lexicon></LexicalResource>
"""
    entries = convert(write_sample(tmp_path / "many.xml", sample))
    synonyms = [r for r in entries["w0"].relations if r.type == "synonym"]
    assert len(synonyms) == 10


def test_every_emitted_markup_passes_the_subset_validator(tmp_path: Path) -> None:
    sample = _SAMPLE.replace("an act of running at a moderate pace", "a &lt;run&gt; &amp; a jog")
    entries = convert(write_sample(tmp_path / "sample.xml", sample))
    for sense in entries["run"].senses:
        assert validate(sense.definition) == ()
        for example in sense.examples:
            assert validate(example.text) == ()


# --- version -----------------------------------------------------------------


def test_bundle_version_uses_the_fetch_month() -> None:
    from datetime import date

    assert bundle_version(date(2026, 2, 4)) == "2026.02.1"
    assert bundle_version(date(2026, 2, 4), 3) == "2026.02.3"


def test_spec_records_the_confirmed_licence() -> None:
    spec = spec_for("2026.02.1")
    assert spec.dict_id == DICT_ID
    assert spec.kind == "monolingual"
    assert spec.source_lang == spec.target_lang == "en"
    assert spec.license == "CC-BY-4.0"
    assert "Open English WordNet" in spec.attribution
    assert "Princeton WordNet" in spec.attribution


# --- fetching ------------------------------------------------------------------


class FakeResponse(io.BytesIO):
    def __init__(self, body: bytes, last_modified: str) -> None:
        super().__init__(body)
        self.headers = Message()
        self.headers["Last-Modified"] = last_modified

    def __enter__(self) -> FakeResponse:
        return self


def test_fetch_writes_the_dump_and_its_info_then_honours_not_modified(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    requests: list[urllib.request.Request] = []

    def first(request: urllib.request.Request, timeout: float) -> FakeResponse:
        requests.append(request)
        return FakeResponse(b"payload", "Wed, 04 Feb 2026 11:20:29 GMT")

    monkeypatch.setattr(urllib.request, "urlopen", first)
    dump = fetch_dump(tmp_path)
    assert dump.read_bytes() == b"payload"
    info = read_dump_info(dump)
    assert info.dump_date.isoformat() == "2026-02-04"
    assert requests[0].get_header("User-agent", "").startswith("omnipipe/")

    def not_modified(request: urllib.request.Request, timeout: float) -> FakeResponse:
        requests.append(request)
        raise urllib.error.HTTPError(request.full_url, 304, "Not Modified", Message(), None)

    monkeypatch.setattr(urllib.request, "urlopen", not_modified)
    assert fetch_dump(tmp_path) == dump
    assert requests[1].get_header("If-modified-since") == "Wed, 04 Feb 2026 11:20:29 GMT"
    assert dump.read_bytes() == b"payload"


def test_fetch_failure_is_an_oewn_error(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    def failing(request: urllib.request.Request, timeout: float) -> FakeResponse:
        raise urllib.error.HTTPError(request.full_url, 404, "Not Found", Message(), None)

    monkeypatch.setattr(urllib.request, "urlopen", failing)
    with pytest.raises(OewnError, match="HTTP 404"):
        fetch_dump(tmp_path)


def test_read_dump_info_missing_sidecar_is_an_error(tmp_path: Path) -> None:
    dump = tmp_path / "english-wordnet.xml.gz"
    dump.write_bytes(b"")
    with pytest.raises(OewnError, match="fetch the dump first"):
        read_dump_info(dump)


# --- converter contract and build ---------------------------------------------


@pytest.fixture
def cached_sample(tmp_path: Path) -> Path:
    cache = tmp_path / "cache"
    cache.mkdir()
    # `dump_path` always names the cached file `english-wordnet.xml.gz`.
    real_dump = cache / "english-wordnet.xml.gz"
    with gzip.open(real_dump, "wt", encoding="utf-8") as handle:
        handle.write(_SAMPLE)
    info = {
        "url": oewn.OEWN_XML_URL,
        "last_modified": "Wed, 04 Feb 2026 11:20:29 GMT",
        "dump_date": "2026-02-04",
        "fetched_at": "2026-02-05T00:00:00Z",
    }
    real_dump.with_name(real_dump.name + ".json").write_text(json.dumps(info), encoding="utf-8")
    return cache


def test_converter_contract_lists_and_builds(cached_sample: Path) -> None:
    converter = OewnConverter(cached_sample)
    specs = converter.dictionaries()
    assert [(s.dict_id, s.version) for s in specs] == [(DICT_ID, "2026.02.1")]
    entries = list(converter.iter_records(specs[0]))
    assert "run" in [e.headword for e in entries]


def test_build_bundle_from_the_sample(cached_sample: Path, tmp_path: Path) -> None:
    converter = OewnConverter(cached_sample)
    spec = converter.dictionaries()[0]
    out = build_bundle(converter.iter_records(spec), spec, tmp_path / "out")
    conn = sqlite3.connect(out)
    try:
        meta = dict(conn.execute("SELECT key, value FROM meta").fetchall())
        assert meta["dict_id"] == DICT_ID
        assert meta["license"] == "CC-BY-4.0"
        assert int(meta["entry_count"]) == 4
        rows = conn.execute("SELECT headword FROM entries WHERE headword_norm = 'run'").fetchall()
        assert rows == [("run",)]
    finally:
        conn.close()


def test_cli_rejects_an_unknown_dictionary(capsys: pytest.CaptureFixture[str]) -> None:
    assert oewn.main(["--dict", "oewn-fr", "--no-fetch"]) == 1
    assert "unknown dictionary" in capsys.readouterr().err
