#!/usr/bin/env python3
"""T0329: gate cross-thread QMetaObject::invokeMethod handoffs against reference capture.

Finds every QMetaObject::invokeMethod call under src/ and flags a
queued/default-connection call whose lambda captures by reference ([&...]).
A cross-thread handoff must carry no reference or pointer to a caller-owned
object: it takes the value by capture, or the receiver owns the state.
Same-thread Qt::DirectConnection calls are allowed and are not flagged.

Run from anywhere: ./scripts/check-thread-handoffs.py [--root DIR]
Exit 0 when clean, 1 with file:line for each finding.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

INVOKE = "QMetaObject::invokeMethod"
REF_CAPTURE = re.compile(r"\[\s*&")
REF_IN_COPY_CAPTURE = re.compile(r"\[\s*=[^\]]*,\s*&")
DIRECT = re.compile(r"Qt::DirectConnection")
EXTS = (".cpp", ".hpp", ".h", ".cc", ".cxx")


def is_comment_line(line: str) -> bool:
    s = line.lstrip()
    return s.startswith("//") or s.startswith("*")


def call_segment(text: str, start: int) -> str:
    """Text of one invokeMethod(...) call, via balanced-paren matching."""
    open_paren = text.find("(", start)
    if open_paren < 0:
        return text[start:start + 500]
    depth = 0
    in_str = in_char = False
    i = open_paren
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        elif in_char:
            if c == "\\":
                i += 1
            elif c == "'":
                in_char = False
        elif c == '"':
            in_str = True
        elif c == "'":
            in_char = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    return text[start:]


def iter_calls(root: Path):
    """Yield (path, lineno, call_text) for each non-comment invokeMethod."""
    for path in sorted(root.rglob("*")):
        if path.suffix not in EXTS or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        lines = text.splitlines()
        for m in re.finditer(re.escape(INVOKE), text):
            lineno = text.count("\n", 0, m.start()) + 1
            if is_comment_line(lines[lineno - 1]):
                continue
            yield path, lineno, call_segment(text, m.start())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default="src",
                    help="directory to scan (default: src, relative to repo root)")
    args = ap.parse_args()

    repo = Path(__file__).resolve().parent.parent
    src = Path(args.root)
    if not src.is_absolute():
        src = repo / src

    findings: list[str] = []
    total = 0
    for path, lineno, call in iter_calls(src):
        total += 1
        if DIRECT.search(call):
            continue
        # QueuedConnection, the completionConnectionType() constant, and any
        # connection argument we cannot prove is direct are treated as
        # queued/default (conservative: flag rather than miss).
        if REF_CAPTURE.search(call) or REF_IN_COPY_CAPTURE.search(call):
            rel = path.relative_to(repo) if path.is_relative_to(repo) else path
            findings.append(f"{rel}:{lineno}: queued/default invokeMethod captures by reference ([&...)")

    if findings:
        for f in findings:
            print(f"FAIL: {f}", file=sys.stderr)
        print(f"{len(findings)} finding(s) over {total} invokeMethod site(s)", file=sys.stderr)
        return 1
    print(f"thread-handoffs OK - {total} invokeMethod site(s), no reference capture in queued/default handoffs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
