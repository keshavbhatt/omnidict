"""FreeDict converter: header/licence detection, TEI mapping, fetch and a full build.

The TEI samples here are hand-written, small fragments in FreeDict's shape; no
FreeDict dictionary text is copied.
"""

from __future__ import annotations

import hashlib
import io
import json
import sqlite3
import tarfile
import urllib.error
import urllib.request
from collections.abc import Sequence
from pathlib import Path
from xml.etree import ElementTree as ET

import pytest

from omnipipe.converters import freedict
from omnipipe.converters.freedict import (
    ConversionStats,
    DatabaseEntry,
    FreeDictConverter,
    FreeDictError,
    convert_tei_entries,
    detect_license,
    dict_id_for,
    fetch_database,
    fetch_release,
    lang_code,
    lang_name,
    list_eligible,
    make_spec,
    map_pos,
    parse_database,
    parse_header_fragment,
    parse_tei_entry,
    read_database,
    version_from_edition,
)
from omnipipe.html_subset import validate
from omnipipe.schema import Entry

TEI_NS = "http://www.tei-c.org/ns/1.0"


def header_bytes(
    availability_inner: str, *, title: str = "Test-English FreeDict Dictionary"
) -> bytes:
    xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<TEI xmlns="{TEI_NS}">
  <teiHeader>
    <fileDesc>
      <titleStmt>
        <title>{title}</title>
        <respStmt>
          <resp>Maintainer</resp>
          <name>Jane Doe</name>
        </respStmt>
      </titleStmt>
      <editionStmt><edition>0.1.2</edition></editionStmt>
      <extent>1500 headwords</extent>
      <publicationStmt>
        <publisher>FreeDict</publisher>
        <availability status="free">
          {availability_inner}
        </availability>
      </publicationStmt>
    </fileDesc>
  </teiHeader>
  <text><body></body></text>
</TEI>"""
    return xml.encode("utf-8")


def entry_xml(inner: str) -> ET.Element:
    """One `<entry>` element parsed from `inner`'s TEI-shaped body."""
    doc = f'<TEI xmlns="{TEI_NS}"><text><body>{inner}</body></text></TEI>'
    root = ET.fromstring(doc)
    body = root.find(f".//{{{TEI_NS}}}body")
    assert body is not None
    return body.find(f"{{{TEI_NS}}}entry")  # type: ignore[return-value]


def convert(entries: Sequence[ET.Element], *, lang: str = "xx") -> list[Entry]:
    stats = ConversionStats()
    return list(convert_tei_entries(entries, lang, stats))


# --- header / licence detection ---------------------------------------------


def test_gpl2_or_later_is_detected() -> None:
    header = parse_header_fragment(
        header_bytes(
            "<p>Available under the terms of the "
            '<ref target="https://www.gnu.org/licenses/gpl-2.0.html">GNU GPL '
            "ver. 2.0 and any later version</ref>.</p>"
        )
    )
    assert header.license_spdx == "GPL-2.0-or-later"
    assert header.license_url == "https://www.gnu.org/licenses/gpl-2.0.html"
    assert header.authors == "Jane Doe"


def test_gpl3_only_without_or_later_wording() -> None:
    header = parse_header_fragment(
        header_bytes(
            "<p>Licensed under the "
            '<ref target="https://www.gnu.org/licenses/gpl-3.0.html">GPLv3</ref>.</p>'
        )
    )
    assert header.license_spdx == "GPL-3.0-only"


def test_cc_by_sa_is_detected() -> None:
    header = parse_header_fragment(
        header_bytes(
            "<p>Licensed under "
            '<ref target="https://creativecommons.org/licenses/by-sa/4.0/">CC BY-SA 4.0</ref>.</p>'
        )
    )
    assert header.license_spdx == "CC-BY-SA-4.0"
    assert header.license_url == "https://creativecommons.org/licenses/by-sa/4.0/"


def test_unknown_licence_is_none_not_guessed() -> None:
    header = parse_header_fragment(
        header_bytes("<p>All rights reserved to the original authors.</p>")
    )
    assert header.license_spdx is None
    assert header.license_url is None


def test_detect_license_names_every_licence_that_applies() -> None:
    # deu-eng's header: GPLv3 and AGPLv3 each cover part of the work.
    result = detect_license(
        "under the terms of both the GPLv3 and the AGPLv3, where each applies to different parts",
        [
            ("https://www.gnu.org/licenses/gpl-3.0.html", "GPLv3"),
            ("https://www.gnu.org/licenses/agpl-3.0.html", "AGPLv3"),
            ("https://www.gnu.org/licenses/gpl-3.0.html", "GPLv3 again"),
        ],
    )
    assert result == ("GPL-3.0-only AND AGPL-3.0-only", "https://www.gnu.org/licenses/gpl-3.0.html")


def test_detect_license_resolves_an_unversioned_gnu_url_from_the_link_text() -> None:
    result = detect_license(
        "some other boilerplate",
        [("https://www.gnu.org/licenses/gpl.html", "GNU GPL, version 2.0")],
    )
    assert result == ("GPL-2.0-only", "https://www.gnu.org/licenses/gpl.html")


def test_detect_license_resolves_an_unversioned_gnu_url_with_or_later_wording() -> None:
    result = detect_license(
        "some other boilerplate",
        [("https://www.gnu.org/licenses/gpl.html", "GPL ver. 3.0 or any later version")],
    )
    assert result == ("GPL-3.0-or-later", "https://www.gnu.org/licenses/gpl.html")


def test_detect_license_accepts_a_plain_text_creative_commons_mention() -> None:
    result = detect_license("Creative Commons Attribution 3.0 Unported License.", [])
    assert result == ("CC-BY-3.0", "https://creativecommons.org/licenses/by/3.0/")


def test_detect_license_accepts_a_plain_text_creative_commons_sharealike_mention() -> None:
    result = detect_license("Creative Commons Attribution-ShareAlike 4.0 licence.", [])
    assert result == ("CC-BY-SA-4.0", "https://creativecommons.org/licenses/by-sa/4.0/")


# --- language codes -----------------------------------------------------------


def test_language_codes_map_to_iso_639_1_where_one_exists() -> None:
    assert lang_code("deu") == "de"
    assert lang_code("eng") == "en"
    assert lang_name("deu") == "German"


def test_language_codes_keep_three_letters_when_no_two_letter_form_exists() -> None:
    assert lang_code("kmr") == "kmr"
    assert lang_name("kmr") == "Northern Kurdish"


def test_unknown_language_code_raises_rather_than_guessing() -> None:
    with pytest.raises(FreeDictError):
        lang_code("xxx")


# --- version mapping -----------------------------------------------------------


def test_dotted_numeric_edition_is_kept_as_is() -> None:
    assert version_from_edition("0.3.5") == "0.3.5"
    assert version_from_edition("2025.11.23") == "2025.11.23"


def test_non_numeric_edition_is_mapped_to_its_digit_groups() -> None:
    assert version_from_edition("1.9-fd1") == "1.9.1"
    assert version_from_edition("2024.10.06+fd1") == "2024.10.06.1"


# --- database JSON parsing (no network) ----------------------------------------


def test_parse_database_skips_non_dictionary_entries() -> None:
    raw = [
        {"software": {"tools": {"URL": "https://example.invalid/tools.tar.gz"}}},
        {
            "name": "deu-eng",
            "edition": "1.9-fd1",
            "headwords": "517534",
            "releases": [
                {
                    "URL": "https://example.invalid/deu-eng.src.tar.xz",
                    "checksum": "AB12",
                    "platform": "src",
                },
                {
                    "URL": "https://example.invalid/deu-eng.slob",
                    "checksum": "cd34",
                    "platform": "slob",
                },
            ],
        },
        {"name": "too-short", "edition": "1.0", "headwords": "5000", "releases": []},
        {"name": "afr-deu", "edition": "0.3.3", "headwords": "3800", "releases": []},
    ]
    entries = parse_database(raw)
    assert [e.name for e in entries] == ["deu-eng"]
    assert entries[0].src_lang3 == "deu"
    assert entries[0].tgt_lang3 == "eng"
    assert entries[0].headwords == 517534
    assert entries[0].src_sha512 == "ab12"


def test_dict_id_and_read_database_roundtrip(tmp_path: Path) -> None:
    raw = [
        {
            "name": "fra-eng",
            "edition": "0.4.1",
            "headwords": "8505",
            "releases": [
                {
                    "URL": "https://example.invalid/fra-eng.src.tar.xz",
                    "checksum": "ff00",
                    "platform": "src",
                }
            ],
        }
    ]
    path = tmp_path / "freedict-database.json"
    path.write_text(json.dumps(raw), encoding="utf-8")
    entries = read_database(path)
    assert dict_id_for(entries[0]) == "freedict-fr-en"


# --- TEI entry mapping ----------------------------------------------------------


def test_multiple_orths_first_is_headword_rest_are_variant_forms() -> None:
    el = entry_xml(
        """
        <entry>
          <form>
            <orth>Abfahrt</orth>
            <form type="abbrev"><orth>Abf.</orth></form>
          </form>
          <sense><cit type="trans"><quote>departure</quote></cit></sense>
        </entry>
        """
    )
    parsed = parse_tei_entry(el, ConversionStats())
    assert parsed is not None
    assert parsed.headword == "Abfahrt"
    assert [(f.form, f.tag) for f in parsed.forms] == [("Abf.", "abbrev")]


def test_pos_and_gender_apply_to_every_sense_of_the_entry() -> None:
    el = entry_xml(
        """
        <entry>
          <form><orth>Bank</orth></form>
          <gramGrp><gen>fem</gen><pos>n</pos></gramGrp>
          <sense n="1"><cit type="trans"><quote>bank</quote></cit></sense>
          <sense n="2"><cit type="trans"><quote>bench</quote></cit></sense>
        </entry>
        """
    )
    parsed = parse_tei_entry(el, ConversionStats())
    assert parsed is not None
    assert [(s.pos, s.pattern) for s in parsed.senses] == [
        ("noun", "feminine"),
        ("noun", "feminine"),
    ]


def test_english_word_and_uppercase_pos_forms_map_correctly() -> None:
    assert map_pos("N") == "noun"
    assert map_pos("Adv.") == "adv"
    assert map_pos("noun") == "noun"
    assert map_pos("N/Adj") == "noun"
    unknown_stats = ConversionStats()
    assert map_pos("Zorp", unknown_stats.unknown_pos) == "other"
    assert unknown_stats.unknown_pos["Zorp"] == 1


def test_translations_join_and_def_text_is_appended() -> None:
    el = entry_xml(
        """
        <entry>
          <form><orth>foo</orth></form>
          <sense>
            <cit type="trans"><quote>bar</quote></cit>
            <cit type="trans"><quote>baz</quote></cit>
            <def>an informal word</def>
          </sense>
        </entry>
        """
    )
    parsed = parse_tei_entry(el, ConversionStats())
    assert parsed is not None
    assert parsed.senses[0].definition == "bar, baz (an informal word)"


def test_sense_without_translation_or_def_is_dropped() -> None:
    el = entry_xml(
        """
        <entry>
          <form><orth>foo</orth></form>
          <sense><usg type="dom">mus.</usg></sense>
        </entry>
        """
    )
    parsed = parse_tei_entry(el, ConversionStats())
    assert parsed is None


def test_usg_becomes_the_sense_label() -> None:
    el = entry_xml(
        """
        <entry>
          <form><orth>Ases</orth></form>
          <sense>
            <usg type="dom">mus.</usg>
            <cit type="trans"><quote>A flat</quote></cit>
          </sense>
        </entry>
        """
    )
    parsed = parse_tei_entry(el, ConversionStats())
    assert parsed is not None
    assert parsed.senses[0].label == "mus."


def test_examples_and_their_translations() -> None:
    el = entry_xml(
        """
        <entry>
          <form><orth>Abfahrt</orth></form>
          <sense>
            <cit type="trans"><quote>departure</quote></cit>
            <cit type="example">
              <quote>Abfahrt des Zuges</quote>
              <cit type="trans"><quote>train departure</quote></cit>
            </cit>
          </sense>
        </entry>
        """
    )
    parsed = parse_tei_entry(el, ConversionStats())
    assert parsed is not None
    assert [(e.text, e.translation) for e in parsed.senses[0].examples] == [
        ("Abfahrt des Zuges", "train departure")
    ]


def test_xr_references_become_relations_with_visible_text_as_target() -> None:
    el = entry_xml(
        """
        <entry>
          <form><orth>A</orth></form>
          <sense>
            <cit type="trans"><quote>A</quote></cit>
            <xr type="syn"><ref target="#Ais.1">Ais</ref><ref target="#As.1">As</ref></xr>
            <xr type="see"><ref target="#Note.1">Note</ref></xr>
          </sense>
        </entry>
        """
    )
    parsed = parse_tei_entry(el, ConversionStats())
    assert parsed is not None
    assert [(r.type, r.target, r.sense_ordinal) for r in parsed.relations] == [
        ("synonym", "Ais", 1),
        ("synonym", "As", 1),
        ("see", "Note", 1),
    ]


def test_ipa_looking_pronunciation_is_kept_non_ipa_is_dropped() -> None:
    ipa_entry = entry_xml(
        """
        <entry>
          <form><orth>Abkhasie</orth><pron>abkaziʃ</pron></form>
          <sense><cit type="trans"><quote>Abkhazia</quote></cit></sense>
        </entry>
        """
    )
    parsed = parse_tei_entry(ipa_entry, ConversionStats())
    assert parsed is not None
    assert parsed.pronunciation is not None
    assert parsed.pronunciation.ipa == "abkaziʃ"

    plain_entry = entry_xml(
        """
        <entry>
          <form><orth>Abkhasie</orth><pron>ABKHASIE</pron></form>
          <sense><cit type="trans"><quote>Abkhazia</quote></cit></sense>
        </entry>
        """
    )
    parsed_plain = parse_tei_entry(plain_entry, ConversionStats())
    assert parsed_plain is not None
    assert parsed_plain.pronunciation is None


def test_consecutive_entries_for_one_headword_merge() -> None:
    entries = [
        entry_xml(
            """
            <entry>
              <form><orth>Bank</orth></form>
              <gramGrp><gen>fem</gen><pos>n</pos></gramGrp>
              <sense><cit type="trans"><quote>bank</quote></cit></sense>
            </entry>
            """
        ),
        entry_xml(
            """
            <entry>
              <form><orth>Bank</orth></form>
              <gramGrp><gen>fem</gen><pos>n</pos></gramGrp>
              <sense><cit type="trans"><quote>bench</quote></cit></sense>
            </entry>
            """
        ),
        entry_xml(
            """
            <entry>
              <form><orth>Boot</orth></form>
              <sense><cit type="trans"><quote>boat</quote></cit></sense>
            </entry>
            """
        ),
    ]
    result = convert(entries)
    assert [e.headword for e in result] == ["Bank", "Boot"]
    assert [s.definition for s in result[0].senses] == ["bank", "bench"]


def test_every_emitted_markup_passes_the_subset_validator() -> None:
    el = entry_xml(
        """
        <entry>
          <form><orth>x &amp; y</orth></form>
          <sense>
            <cit type="trans"><quote>a &lt;b&gt; c</quote></cit>
            <cit type="example">
              <quote>1 &lt; 2</quote>
              <cit type="trans"><quote>x &amp; y</quote></cit>
            </cit>
          </sense>
        </entry>
        """
    )
    parsed = parse_tei_entry(el, ConversionStats())
    assert parsed is not None
    texts = [parsed.senses[0].definition]
    for example in parsed.senses[0].examples:
        texts.append(example.text)
        if example.translation is not None:
            texts.append(example.translation)
    for text in texts:
        assert validate(text) == (), text


# --- fetch / build --------------------------------------------------------------


class FakeResponse(io.BytesIO):
    """A minimal stand-in for `http.client.HTTPResponse`: bytes plus headers."""

    def __init__(
        self, body: bytes, *, last_modified: str = "Fri, 25 Sep 2026 10:15:23 GMT"
    ) -> None:
        super().__init__(body)
        from email.message import Message

        self.headers = Message()
        self.headers["Last-Modified"] = last_modified

    def __enter__(self) -> FakeResponse:
        return self


def make_tarball(name: str, tei_bytes: bytes) -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:xz") as tar:
        info = tarfile.TarInfo(name=f"{name}/{name}.tei")
        info.size = len(tei_bytes)
        tar.addfile(info, io.BytesIO(tei_bytes))
    return buffer.getvalue()


def full_tei(entries_xml: str, *, availability: str | None = None) -> bytes:
    if availability is None:
        availability = (
            "<p>Available under the "
            '<ref target="https://www.gnu.org/licenses/gpl-2.0.html">'
            "GPL 2.0 or any later version</ref>.</p>"
        )
    header = header_bytes(availability, title="Test-English FreeDict Dictionary").decode("utf-8")
    header = header.replace(
        "<text><body></body></text>", f"<text><body>{entries_xml}</body></text>"
    )
    return header.encode("utf-8")


def test_fetch_release_verifies_checksum_and_caches(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    payload = make_tarball(
        "fra-eng",
        full_tei(
            "<entry><form><orth>chat</orth></form>"
            '<sense><cit type="trans"><quote>cat</quote></cit></sense></entry>'
        ),
    )
    digest = hashlib.sha512(payload).hexdigest()
    entry = DatabaseEntry(
        name="fra-eng",
        src_lang3="fra",
        tgt_lang3="eng",
        edition="0.4.1",
        headwords=8505,
        src_url="https://example.invalid/freedict-fra-eng-0.4.1.src.tar.xz",
        src_sha512=digest,
    )

    calls: list[str] = []

    def fake_urlopen(request: urllib.request.Request, timeout: float) -> io.BytesIO:
        calls.append(request.full_url)
        return io.BytesIO(payload)

    monkeypatch.setattr(urllib.request, "urlopen", fake_urlopen)
    path = fetch_release(tmp_path, entry)
    assert path.read_bytes() == payload
    assert len(calls) == 1

    # Cached: a second call must not hit the network again.
    fetch_release(tmp_path, entry)
    assert len(calls) == 1


def test_fetch_release_rejects_a_checksum_mismatch(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    payload = make_tarball("fra-eng", full_tei(""))
    entry = DatabaseEntry(
        name="fra-eng",
        src_lang3="fra",
        tgt_lang3="eng",
        edition="0.4.1",
        headwords=8505,
        src_url="https://example.invalid/freedict-fra-eng-0.4.1.src.tar.xz",
        src_sha512="0" * 128,
    )

    def fake_urlopen(request: urllib.request.Request, timeout: float) -> io.BytesIO:
        return io.BytesIO(payload)

    monkeypatch.setattr(urllib.request, "urlopen", fake_urlopen)
    with pytest.raises(FreeDictError, match="checksum mismatch"):
        fetch_release(tmp_path, entry)
    assert list((tmp_path / "fra-eng").glob("*.src.tar.xz")) == []


def test_fetch_database_writes_the_index_and_its_sidecar(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    body = json.dumps([{"name": "fra-eng"}]).encode("utf-8")

    def fake_urlopen(request: urllib.request.Request, timeout: float) -> FakeResponse:
        return FakeResponse(body)

    monkeypatch.setattr(urllib.request, "urlopen", fake_urlopen)
    path = fetch_database(tmp_path)
    assert path.read_bytes() == body
    assert path.with_name(path.name + ".json").exists()


def test_fetch_database_failure_is_a_freedict_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def failing(request: urllib.request.Request, timeout: float) -> None:
        from email.message import Message

        raise urllib.error.HTTPError(request.full_url, 404, "Not Found", Message(), None)

    monkeypatch.setattr(urllib.request, "urlopen", failing)
    with pytest.raises(FreeDictError, match="HTTP 404"):
        fetch_database(tmp_path)


def test_converter_builds_a_full_bundle(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    tei_entries = (
        "<entry><form><orth>chat</orth></form><gramGrp><gen>masc</gen><pos>n</pos></gramGrp>"
        '<sense><cit type="trans"><quote>cat</quote></cit>'
        '<cit type="example"><quote>un petit chat</quote>'
        '<cit type="trans"><quote>a small cat</quote></cit></cit></sense></entry>'
        "<entry><form><orth>chien</orth></form>"
        '<sense><cit type="trans"><quote>dog</quote></cit></sense></entry>'
    )
    payload = make_tarball("fra-eng", full_tei(tei_entries))
    digest = hashlib.sha512(payload).hexdigest()
    cache = tmp_path / "cache"

    database = [
        {
            "name": "fra-eng",
            "edition": "0.4.1",
            "headwords": "8505",
            "releases": [
                {
                    "URL": "https://example.invalid/freedict-fra-eng-0.4.1.src.tar.xz",
                    "checksum": digest,
                    "platform": "src",
                }
            ],
        }
    ]

    def fake_urlopen(request: urllib.request.Request, timeout: float) -> io.BytesIO:
        if request.full_url == freedict.FREEDICT_DATABASE_URL:
            return FakeResponse(json.dumps(database).encode("utf-8"))
        return io.BytesIO(payload)

    monkeypatch.setattr(urllib.request, "urlopen", fake_urlopen)

    out = tmp_path / "out"
    code = freedict.main(
        [
            "--dict",
            "freedict-fr-en",
            "--cache",
            str(cache),
            "--out",
            str(out),
            "--built-at",
            "2026-09-27T00:00:00Z",
        ]
    )
    assert code == 0

    conn = sqlite3.connect(out / "freedict-fr-en" / "dict.sqlite")
    meta = dict(conn.execute("SELECT key, value FROM meta").fetchall())
    assert meta["dict_id"] == "freedict-fr-en"
    assert meta["source_lang"] == "fr"
    assert meta["target_lang"] == "en"
    assert meta["version"] == "0.4.1"
    assert meta["license"] == "GPL-2.0-or-later"
    assert meta["source_converter"] == "freedict"
    assert int(meta["entry_count"]) == 2
    rows = conn.execute("SELECT headword FROM entries ORDER BY headword").fetchall()
    assert rows == [("chat",), ("chien",)]
    conn.close()

    # dictionaries() lists what has actually been fetched (via read_header_from_release).
    converter = FreeDictConverter(cache)
    specs = converter.dictionaries()
    assert [s.dict_id for s in specs] == ["freedict-fr-en"]


def test_list_eligible_counts_exclusions_by_reason(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    good = DatabaseEntry(
        name="fra-eng",
        src_lang3="fra",
        tgt_lang3="eng",
        edition="0.4.1",
        headwords=8505,
        src_url="https://example.invalid/good.src.tar.xz",
        src_sha512="a" * 128,
    )
    too_small = DatabaseEntry(
        name="spa-por",
        src_lang3="spa",
        tgt_lang3="por",
        edition="0.1",
        headwords=500,
        src_url="https://example.invalid/small.src.tar.xz",
        src_sha512="b" * 128,
    )
    unclear = DatabaseEntry(
        name="deu-por",
        src_lang3="deu",
        tgt_lang3="por",
        edition="0.1",
        headwords=5000,
        src_url="https://example.invalid/unclear.src.tar.xz",
        src_sha512="c" * 128,
    )

    good_payload = make_tarball("fra-eng", full_tei(""))
    unclear_payload = make_tarball(
        "deu-por", full_tei("", availability="<p>All rights reserved.</p>")
    )

    def fake_urlopen(request: urllib.request.Request, timeout: float) -> io.BytesIO:
        if "good" in request.full_url:
            return io.BytesIO(good_payload)
        if "unclear" in request.full_url:
            return io.BytesIO(unclear_payload)
        raise AssertionError(f"unexpected fetch of {request.full_url}")

    monkeypatch.setattr(urllib.request, "urlopen", fake_urlopen)
    eligible, reasons = list_eligible(tmp_path, [good, too_small, unclear])
    assert [dict_id_for(e) for e, _h in eligible] == ["freedict-fr-en"]
    assert reasons["too few headwords"] == 1
    assert reasons["no recognised free licence"] == 1


def test_make_spec_uses_authors_or_default_publisher() -> None:
    entry = DatabaseEntry(
        name="fra-eng",
        src_lang3="fra",
        tgt_lang3="eng",
        edition="0.4.1",
        headwords=8505,
        src_url="https://example.invalid/x.src.tar.xz",
        src_sha512="a" * 128,
    )
    header = parse_header_fragment(
        header_bytes(
            '<p><ref target="https://www.gnu.org/licenses/gpl-2.0.html">GPL 2.0 or later</ref></p>'
        )
    )
    spec = make_spec(entry, header)
    assert spec.dict_id == "freedict-fr-en"
    assert spec.publisher == "Jane Doe"
    assert spec.kind == "bilingual"
    assert "Jane Doe" in spec.attribution
    assert spec.license == "GPL-2.0-or-later"


def _tarball_with(files: dict[str, bytes]) -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:xz") as tar:
        for path, data in files.items():
            info = tarfile.TarInfo(name=path)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


def _entry_for(name: str) -> DatabaseEntry:
    src, tgt = name.split("-")
    return DatabaseEntry(
        name=name,
        src_lang3=src,
        tgt_lang3=tgt,
        edition="0.1.1",
        headwords=5000,
        src_url="https://example.invalid/x.src.tar.xz",
        src_sha512="",
    )


def _headwords(tarball: bytes, name: str, tmp_path: Path) -> list[str]:
    path = tmp_path / "t.tar.xz"
    path.write_bytes(tarball)
    stats = freedict.ConversionStats()
    return [e.headword for e in freedict.convert_tei_file(path, _entry_for(name), stats)]


def test_translation_marked_on_the_quote(tmp_path: Path) -> None:
    # spa-ast, oci-cat, mkd-bul: an untyped <cit> holding <quote type="trans">.
    tei = full_tei(
        '<entry><form><orth>aún</orth></form><sense><cit><quote type="trans">entá</quote>'
        "</cit></sense></entry>"
    )
    path = tmp_path / "t.tar.xz"
    path.write_bytes(make_tarball("spa-ast", tei))
    entries = list(
        freedict.convert_tei_file(path, _entry_for("spa-ast"), freedict.ConversionStats())
    )
    assert [e.headword for e in entries] == ["aún"]
    assert entries[0].senses[0].definition == "entá"


def test_the_dictionary_file_is_chosen_over_a_header_only_file(tmp_path: Path) -> None:
    # lat-deu ships lat-deu-header.tei before lat-deu.tei.
    header_only = full_tei("")
    real = full_tei(
        '<entry><form><orth>aqua</orth></form><sense><cit type="trans"><quote>Wasser</quote>'
        "</cit></sense></entry>"
    )
    tarball = _tarball_with(
        {"lat-deu/lat-deu-header.tei": header_only, "lat-deu/lat-deu.tei": real}
    )
    assert _headwords(tarball, "lat-deu", tmp_path) == ["aqua"]


def test_included_files_are_followed_in_place(tmp_path: Path) -> None:
    # eng-pol keeps one file per letter behind <xi:include>.
    main = full_tei(
        '<include href="letters/a.xml" xmlns="http://www.w3.org/2001/XInclude"/>'
        '<include href="letters/b.xml" xmlns="http://www.w3.org/2001/XInclude"/>'
        '<include href="letters/missing.xml" xmlns="http://www.w3.org/2001/XInclude"/>'
    )

    def letter(word: str) -> bytes:
        return (
            '<div xmlns="http://www.tei-c.org/ns/1.0"><entry><form><orth>'
            f'{word}</orth></form><sense><cit type="trans"><quote>x</quote></cit></sense>'
            "</entry></div>"
        ).encode()

    tarball = _tarball_with(
        {
            "eng-pol/eng-pol.tei": main,
            "eng-pol/letters/a.xml": letter("apple"),
            "eng-pol/letters/b.xml": letter("bread"),
        }
    )
    assert _headwords(tarball, "eng-pol", tmp_path) == ["apple", "bread"]


def test_nested_senses_give_their_innermost_translations(tmp_path: Path) -> None:
    # eng-pol: <sense level="0"><xr/><sense level="1"><sense level="2"><cit/>.
    tei = full_tei(
        '<entry><form><orth>AA</orth></form><sense level="0"><xr><ref>Alcoholics Anonymous</ref>'
        '</xr><sense level="1"><sense level="2"><cit type="trans"><quote>Anonimowi Alkoholicy'
        "</quote></cit></sense></sense></sense></entry>"
    )
    path = tmp_path / "t.tar.xz"
    path.write_bytes(make_tarball("eng-pol", tei))
    entries = list(
        freedict.convert_tei_file(path, _entry_for("eng-pol"), freedict.ConversionStats())
    )
    assert [s.definition for s in entries[0].senses] == ["Anonimowi Alkoholicy"]
    assert [r.target for r in entries[0].relations] == ["Alcoholics Anonymous"]
