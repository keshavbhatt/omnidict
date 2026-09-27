"""Restricted HTML subset for entry definitions and examples (PLAN.md 4.3).

Allowed markup: `b i u sub sup br span a`, with `span` restricted to a single
`class` attribute from a fixed set, and `a` restricted to a single `href`
attribute using the `lex:` scheme. Everything else is stripped. Converters
must produce output that sanitizes with zero warnings; `build.py` treats any
warning as a hard error.
"""

from __future__ import annotations

import html
import re
from dataclasses import dataclass
from html.parser import HTMLParser

_ALLOWED_TAGS = frozenset({"b", "i", "u", "sub", "sup", "br", "span", "a"})
_VOID_TAGS = frozenset({"br"})
_SPAN_CLASSES = frozenset({"pos", "label", "pattern", "hw"})
_HREF_SCHEME = "lex:"

_WHITESPACE_RE = re.compile(r"\s+")


@dataclass(frozen=True, slots=True)
class Sanitized:
    """Result of sanitizing a restricted-HTML fragment."""

    html: str
    warnings: tuple[str, ...]


@dataclass(slots=True)
class _Frame:
    """One entry on the open-tag stack."""

    name: str
    emit: bool  # whether this tag produces output open/close markup
    close_text: str = ""


def _escape_text(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class _Sanitizer(HTMLParser):
    """Parses arbitrary HTML and rewrites it into the restricted subset."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self._out: list[str] = []
        self._warnings: list[str] = []
        self._stack: list[_Frame] = []
        self._raw_skip_tag: str | None = None  # set while inside <script>/<style>

    def result(self) -> Sanitized:
        # Close anything still open at end of input.
        while self._stack:
            frame = self._stack.pop()
            self._warnings.append(f"unclosed tag <{frame.name}> was closed")
            if frame.emit:
                self._out.append(f"</{frame.name}>")
        return Sanitized(html="".join(self._out), warnings=tuple(self._warnings))

    # -- HTMLParser callbacks -------------------------------------------------

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self._start(tag, attrs)

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        # Treat self-closed tags the same as a plain start tag; void tags
        # (br) need nothing further, non-void self-closed tags stay "open"
        # and are closed normally by a later end tag or at end of input.
        self._start(tag, attrs)

    def handle_endtag(self, tag: str) -> None:
        name = tag.lower()
        if self._raw_skip_tag is not None:
            if name == self._raw_skip_tag:
                self._raw_skip_tag = None
            return
        if self._stack and self._stack[-1].name == name:
            frame = self._stack.pop()
            if frame.emit:
                self._out.append(f"</{frame.name}>")
        else:
            self._warnings.append(f"dropped stray end tag </{name}>")

    def handle_data(self, data: str) -> None:
        if self._raw_skip_tag is not None:
            return
        if data:
            self._out.append(_escape_text(data))

    def handle_comment(self, data: str) -> None:
        if self._raw_skip_tag is None:
            self._warnings.append("dropped comment")

    def handle_decl(self, decl: str) -> None:
        if self._raw_skip_tag is None:
            self._warnings.append("dropped doctype")

    def unknown_decl(self, data: str) -> None:
        if self._raw_skip_tag is None:
            self._warnings.append("dropped unknown declaration")

    def handle_pi(self, data: str) -> None:
        if self._raw_skip_tag is None:
            self._warnings.append("dropped processing instruction")

    # -- helpers ---------------------------------------------------------------

    def _start(self, raw_tag: str, attrs: list[tuple[str, str | None]]) -> None:
        name = raw_tag.lower()

        if self._raw_skip_tag is not None:
            return  # everything inside script/style is dropped

        if name in ("script", "style"):
            self._warnings.append(f"removed disallowed tag <{name}> and its content")
            self._raw_skip_tag = name
            return

        if name not in _ALLOWED_TAGS:
            self._warnings.append(f"removed disallowed tag <{name}>, kept text content")
            self._stack.append(_Frame(name=name, emit=False))
            return

        if name == "span":
            self._start_span(attrs)
        elif name == "a":
            self._start_anchor(attrs)
        else:
            self._start_plain(name, attrs)

    def _start_plain(self, name: str, attrs: list[tuple[str, str | None]]) -> None:
        for attr_name, _ in attrs:
            self._warnings.append(f"dropped attribute {attr_name.lower()} on <{name}>")
        if name in _VOID_TAGS:
            self._out.append(f"<{name}>")
            return
        self._out.append(f"<{name}>")
        self._stack.append(_Frame(name=name, emit=True))

    def _start_span(self, attrs: list[tuple[str, str | None]]) -> None:
        class_value: str | None = None
        class_seen = False
        extra: list[str] = []
        for attr_name, attr_value in attrs:
            lowered = attr_name.lower()
            if lowered == "class" and not class_seen:
                class_seen = True
                class_value = attr_value
            else:
                extra.append(lowered)

        valid = class_seen and class_value in _SPAN_CLASSES
        if not valid:
            self._warnings.append("unwrapped <span> with missing or invalid class")
            self._stack.append(_Frame(name="span", emit=False))
            return

        for attr_name in extra:
            self._warnings.append(f"dropped attribute {attr_name} on <span>")
        assert class_value is not None
        escaped = html.escape(class_value, quote=True)
        self._out.append(f'<span class="{escaped}">')
        self._stack.append(_Frame(name="span", emit=True))

    def _start_anchor(self, attrs: list[tuple[str, str | None]]) -> None:
        href_value: str | None = None
        href_seen = False
        extra = []
        for attr_name, attr_value in attrs:
            lowered = attr_name.lower()
            if lowered == "href" and not href_seen:
                href_seen = True
                href_value = attr_value
            else:
                extra.append(lowered)

        valid = (
            href_seen
            and href_value is not None
            and href_value.startswith(_HREF_SCHEME)
            and len(href_value) > len(_HREF_SCHEME)
        )
        if not valid:
            self._warnings.append("unwrapped <a> with missing or invalid href")
            self._stack.append(_Frame(name="a", emit=False))
            return

        for attr_name in extra:
            self._warnings.append(f"dropped attribute {attr_name} on <a>")
        assert href_value is not None
        escaped = html.escape(href_value, quote=True)
        self._out.append(f'<a href="{escaped}">')
        self._stack.append(_Frame(name="a", emit=True))


def sanitize(text: str) -> Sanitized:
    """Rewrite `text` into the restricted HTML subset, reporting every change."""
    parser = _Sanitizer()
    parser.feed(text)
    parser.close()
    return parser.result()


def validate(text: str) -> tuple[str, ...]:
    """Return the warnings that `sanitize` would produce for `text`."""
    return sanitize(text).warnings


def is_valid(text: str) -> bool:
    """Return True if `text` sanitizes with zero warnings."""
    return len(validate(text)) == 0


class _PlainTextExtractor(HTMLParser):
    """Strips all tags, turning `<br>` into a space."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self._parts: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag.lower() == "br":
            self._parts.append(" ")

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag.lower() == "br":
            self._parts.append(" ")

    def handle_data(self, data: str) -> None:
        self._parts.append(data)

    def text(self) -> str:
        return "".join(self._parts)


def to_plain(text: str) -> str:
    """Strip all markup and return collapsed, trimmed plain text."""
    parser = _PlainTextExtractor()
    parser.feed(text)
    parser.close()
    collapsed = _WHITESPACE_RE.sub(" ", parser.text())
    return collapsed.strip()
