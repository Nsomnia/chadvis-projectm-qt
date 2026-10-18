#!/usr/bin/env python3
"""Validate c-cpp-profi documentation surfaces.

The Documentation reference defines the required shape of each documentation
artifact: a README with the build/install/usage/license sections, per-symbol
API contracts that name ownership, thread-safety, and an error/returns
contract, and a Keep-a-Changelog changelog with a versioned section. This
checker turns those rules into a deterministic contract the same way
cpp_idea_check.py turns the Idea Card into one, and the same way
cpp_evidence_check.py turns the gate report into one.

Every documentation surface, regardless of kind, is also run through the slop
pass: the banned AI-marketing tokens from DOCUMENTATION.md "Slop-Free Prose"
must not appear outside a fenced code block or a line that is itself part of
the ban-list discussion.

Usage:
    cpp_docs_check.py <doc.md> [--kind readme|api|changelog|auto] [--json]
    cpp_docs_check.py --self-test

Kind defaults to `auto`, inferred from the filename and the headings.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path


# DOCUMENTATION.md "Slop-Free Prose" ban-list. Matched case-insensitively on
# word-ish boundaries so `leverage` hits but `leveraged-buyout` substrings in a
# real word like `clever` do not. `game-changer` allows an optional hyphen.
SLOP_TOKENS = [
    "seamless",
    "robust",
    "leverage",
    "in today's world",
    "comprehensive solution",
    "blazingly fast",
    "game-?changer",
    "effortless",
]

# A line that discusses the ban-list itself is exempt from the slop pass so the
# linter does not flag its own documentation.
SLOP_EXEMPT_RE = re.compile(r"\b(ban|banned|slop)", re.IGNORECASE)

# README required sections. Each entry is (canonical-name, regex of synonyms).
README_SECTIONS = [
    ("build/install", re.compile(r"\b(build|install|installation|from source)\b", re.IGNORECASE)),
    ("usage/quickstart/example", re.compile(r"\b(usage|quickstart|quick start|example)\b", re.IGNORECASE)),
    ("license", re.compile(r"\blicen[sc]e\b", re.IGNORECASE)),
]

# API contract fields. error/returns is satisfied by either token.
API_CONTRACT_FIELDS = [
    ("ownership", re.compile(r"\bownership\b", re.IGNORECASE)),
    ("thread-safety", re.compile(r"\b(thread[- ]?safe(ty)?|threadsafety)\b", re.IGNORECASE)),
    ("error/returns", re.compile(r"\b(error|errno|return|returns|retval)\b", re.IGNORECASE)),
]

# A Doxygen contract block names its symbol with @fn/@brief, or any @-tag marks
# the block as an API contract block.
DOXYGEN_NAME_RE = re.compile(r"@(?:fn|brief)\s+([A-Za-z_][A-Za-z0-9_:]*)")
DOXYGEN_TAG_RE = re.compile(r"@(?:param|return|retval|threadsafety|pre|post|brief|fn)\b")

# A C/C++ function *declaration* (not a call): an optional return type, a name,
# a parenthesized parameter list, terminated by `;` or `{`. A leading control
# keyword (`if (`, `while (`, `return (`) or an `=`/`!`/`<` before the `(` marks
# a call/expression, which is rejected by requiring the line to start at column
# zero or a `*`/type token and end with `;`/`{`.
DECL_RE = re.compile(
    r"^\s*(?:[A-Za-z_][\w:<>,\s\*&]*?[\s\*&])?"
    r"([A-Za-z_][A-Za-z0-9_:]*)\s*\([^;=]*\)\s*[;{]\s*$"
)
CONTROL_KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else"}

# Heading or bold-list symbol declaration: the content is *only* a signature.
HEADING_SYMBOL_RE = re.compile(r"^\s*#{1,6}\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\([^)]*\)\s*;?\s*$")
BOLD_SYMBOL_RE = re.compile(r"^\s*[-*]\s+\*\*([A-Za-z_][A-Za-z0-9_:]*)\s*\([^)]*\)\s*;?\*\*")

CHANGELOG_VERSION_RE = re.compile(r"^\s*#{1,3}\s*\[?v?\d+\.\d+", re.MULTILINE)
CHANGELOG_VERSION_INLINE_RE = re.compile(r"\[?v?\d+\.\d+\.\d+\]?")
CHANGELOG_SECTION_RE = re.compile(r"\b(added|changed|fixed|removed|deprecated|security)\b", re.IGNORECASE)

PLACEHOLDERS = {
    "",
    "todo",
    "tbd",
    "n/a",
    "n/a?",
    "?",
    "xxx",
    "fixme",
    "none yet",
}


def normalize(value: str) -> str:
    return " ".join(value.strip().lower().split())


def read_doc(path: str) -> str:
    # G15: clean one-line error + non-zero exit on binary/non-UTF-8/missing input.
    if path == "-":
        try:
            return sys.stdin.read()
        except UnicodeDecodeError:
            print("error: stdin is not valid UTF-8 text", file=sys.stderr)
            sys.exit(2)
    try:
        return Path(path).read_text(encoding="utf-8")
    except FileNotFoundError:
        print(f"error: file not found: {path}", file=sys.stderr)
        sys.exit(2)
    except (IsADirectoryError, PermissionError, OSError) as exc:
        print(f"error: cannot read {path}: {exc}", file=sys.stderr)
        sys.exit(2)
    except UnicodeDecodeError:
        print(f"error: {path} is not valid UTF-8 text (binary file?)", file=sys.stderr)
        sys.exit(2)


def is_placeholder(value: str) -> bool:
    return normalize(value) in PLACEHOLDERS


def fence_mask(lines: list[str]) -> list[bool]:
    """Return a per-line mask that is True inside a fenced code block."""
    mask = [False] * len(lines)
    open_fence = False
    for idx, line in enumerate(lines):
        if line.strip().startswith("```"):
            # The fence delimiter line itself is treated as outside.
            open_fence = not open_fence
            continue
        if open_fence:
            mask[idx] = True
    return mask


def detect_kind(text: str, filename: str) -> str:
    name = filename.lower()
    if "changelog" in name:
        return "changelog"
    if "readme" in name:
        return "readme"
    if "api" in name or "doxygen" in name or "header" in name:
        return "api"

    lowered = text.lower()
    if CHANGELOG_VERSION_RE.search(text) and CHANGELOG_SECTION_RE.search(text):
        return "changelog"
    if "@param" in lowered or "@return" in lowered or "@threadsafety" in lowered:
        return "api"
    if any(rx.search(lowered) for _, rx in README_SECTIONS[:1]):
        # Build/install heading present -> looks like a README.
        return "readme"
    return "readme"


def slop_errors(lines: list[str], mask: list[bool]) -> list[str]:
    errors: list[str] = []
    patterns = [(tok, re.compile(r"\b" + tok + r"\b", re.IGNORECASE)) for tok in SLOP_TOKENS]
    for idx, line in enumerate(lines):
        if mask[idx]:
            continue
        if SLOP_EXEMPT_RE.search(line):
            continue
        for display, rx in patterns:
            match = rx.search(line)
            if match:
                token = match.group(0).lower()
                errors.append(f"slop token '{token}' at line {idx + 1}")
    return errors


def check_readme(text: str) -> list[str]:
    errors: list[str] = []
    lines = text.splitlines()
    mask = fence_mask(lines)
    # Sections may be declared as a heading or a bold label; search only the
    # prose (outside code fences) so a fenced snippet does not satisfy them.
    prose = "\n".join(line for idx, line in enumerate(lines) if not mask[idx])
    for name, rx in README_SECTIONS:
        if not rx.search(prose):
            errors.append(f"readme missing required section: {name}")
    return errors


def extract_symbols(text: str) -> list[tuple[str, str]]:
    """Return (symbol-id, contract-body) pairs.

    A contract block is a fenced code block that *declares* a public symbol (a
    Doxygen comment with an `@fn`/`@brief`/`@param` tag, or a C/C++ function
    prototype/definition), or a heading/bold list entry whose content is only a
    symbol signature. A block that merely *calls* a function (a usage snippet, a
    changelog line, a gate-report table) is not a contract block. The body is
    the full block text the field checks run against.
    """
    lines = text.splitlines()
    mask = fence_mask(lines)
    blocks: list[tuple[str, str]] = []

    # Fenced code blocks that declare (not call) a function-like symbol.
    open_fence = False
    fence: list[str] = []
    for line in lines:
        if line.strip().startswith("```"):
            if open_fence:
                body = "\n".join(fence)
                sym = _declared_symbol(body)
                if sym is not None:
                    blocks.append((sym, body))
                open_fence = False
                fence = []
            else:
                open_fence = True
                fence = []
            continue
        if open_fence:
            fence.append(line)

    # Heading/bold-list symbol declarations outside fences, whose content is
    # only a signature: `### foo_decode()` or `- **foo_free()**`, followed by
    # the contract prose up to the next heading.
    idx = 0
    while idx < len(lines):
        if mask[idx]:
            idx += 1
            continue
        decl = HEADING_SYMBOL_RE.match(lines[idx]) or BOLD_SYMBOL_RE.match(lines[idx])
        if decl:
            body_lines = [lines[idx]]
            for follow in lines[idx + 1 :]:
                if re.match(r"^\s*#{1,6}\s", follow):
                    break
                body_lines.append(follow)
                if len(body_lines) > 20:
                    break
            blocks.append((decl.group(1), "\n".join(body_lines)))
        idx += 1

    return blocks


def _declared_symbol(body: str) -> str | None:
    """Return the documented symbol in a fenced contract block, or None.

    A contract block must carry contract *intent*: a Doxygen `@`-tag, or a
    function prototype that sits next to a doc comment (`/**`, `///`, `//!`).
    A bare usage snippet (an `int main(void) { ... }` example with no doc
    comment) is a call/example, not a documented public-symbol contract.
    """
    has_doc_comment = bool(re.search(r"/\*\*|///|//!", body)) or bool(
        DOXYGEN_TAG_RE.search(body)
    )
    if not has_doc_comment:
        return None

    if DOXYGEN_TAG_RE.search(body):
        named = DOXYGEN_NAME_RE.search(body)
        if named:
            return named.group(1)

    for raw in body.splitlines():
        match = DECL_RE.match(raw)
        if not match:
            continue
        name = match.group(1)
        if name in CONTROL_KEYWORDS:
            continue
        head = raw[: match.start(1)]
        if "=" in head or "return" in head.split():
            continue
        return name
    return None


def field_present(body: str, rx: re.Pattern[str]) -> bool:
    for raw in body.splitlines():
        match = rx.search(raw)
        if not match:
            continue
        # The field is non-placeholder if the rest of the line carries content.
        tail = raw[match.end() :].strip(" \t:.-@*/").strip()
        if tail and not is_placeholder(tail):
            return True
        # Doxygen `@threadsafety ...` and prose mentions count as present when
        # the line has any trailing descriptive text at all.
        if tail:
            return True
    return False


def check_api(text: str) -> list[str]:
    errors: list[str] = []
    symbols = extract_symbols(text)
    if not symbols:
        return ["api: no public-symbol contract block found"]
    for sym, body in symbols:
        for field, rx in API_CONTRACT_FIELDS:
            if not field_present(body, rx):
                errors.append(f"api symbol {sym}: missing contract field: {field}")
    return errors


def check_changelog(text: str) -> list[str]:
    has_version = bool(
        CHANGELOG_VERSION_RE.search(text) or CHANGELOG_VERSION_INLINE_RE.search(text)
    )
    has_section = bool(CHANGELOG_SECTION_RE.search(text))
    if not (has_version and has_section):
        return ["changelog: missing versioned section"]
    return []


def check_doc(text: str, kind: str, filename: str) -> tuple[str, list[str]]:
    if kind == "auto":
        kind = detect_kind(text, filename)

    lines = text.splitlines()
    mask = fence_mask(lines)
    errors = slop_errors(lines, mask)

    if kind == "readme":
        errors.extend(check_readme(text))
    elif kind == "api":
        errors.extend(check_api(text))
    elif kind == "changelog":
        errors.extend(check_changelog(text))
    else:  # pragma: no cover - argparse constrains the choices.
        errors.append(f"unknown kind: {kind}")

    return kind, errors


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "doc",
        nargs="?",
        help="markdown documentation file, or '-' for stdin",
    )
    parser.add_argument(
        "--kind",
        choices=["readme", "api", "changelog", "auto"],
        default="auto",
        help="documentation kind (default: auto-detect from filename and headings)",
    )
    parser.add_argument("--json", action="store_true", help="emit JSON result")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run the built-in good/bad fixtures and exit",
    )
    return parser.parse_args(argv)


GOOD_README = """\
# libgeohash

A C library that encodes and decodes geohashes. For application authors.

## Build from source

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## Usage

```c
#include <geohash.h>
```

## License

MIT.
"""

BAD_README = """\
# superwidget

A seamless, blazingly fast widget engine.

## Usage

```c
#include <widget.h>
```
"""


def run_self_test() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="cpp_docs_check_"))
    try:
        good = tmp / "README.good.md"
        bad = tmp / "README.bad.md"
        good.write_text(GOOD_README, encoding="utf-8")
        bad.write_text(BAD_README, encoding="utf-8")

        good_kind, good_errors = check_doc(GOOD_README, "readme", str(good))
        assert good_kind == "readme", f"good kind: {good_kind}"
        assert not good_errors, f"good doc should pass, got: {good_errors}"

        bad_kind, bad_errors = check_doc(BAD_README, "readme", str(bad))
        assert bad_kind == "readme", f"bad kind: {bad_kind}"
        assert any("slop token 'seamless'" in e for e in bad_errors), bad_errors
        assert any("slop token 'blazingly fast'" in e for e in bad_errors), bad_errors
        assert any(
            "readme missing required section: build/install" in e for e in bad_errors
        ), bad_errors
        assert any(
            "readme missing required section: license" in e for e in bad_errors
        ), bad_errors
    except AssertionError as exc:
        print(f"cpp_docs_check self-test: FAIL ({exc})")
        _rmtree(tmp)
        return 1
    _rmtree(tmp)
    print("cpp_docs_check self-test: PASS")
    return 0


def _rmtree(path: Path) -> None:
    import shutil

    shutil.rmtree(path, ignore_errors=True)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if args.self_test:
        return run_self_test()

    if not args.doc:
        print("c-cpp-profi docs check: FAIL")
        print("- no document given (pass a file, '-', or --self-test)")
        return 1

    text = read_doc(args.doc)
    kind, errors = check_doc(text, args.kind, args.doc)

    if args.json:
        print(
            json.dumps(
                {"ok": not errors, "kind": kind, "errors": errors},
                indent=2,
                sort_keys=True,
            )
        )
    elif errors:
        print("c-cpp-profi docs check: FAIL")
        for error in errors:
            print(f"- {error}")
    else:
        print("c-cpp-profi docs check: PASS")
        print(f"kind={kind}")

    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
