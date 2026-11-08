#!/usr/bin/env python3
"""T0438: forbid Q_ASSERT in tests.

The Windows and Linux gates configure Release, where Qt defines QT_NO_DEBUG and
Q_ASSERT compiles to nothing. An assertion whose argument has a side effect then
stops doing the work as well as the checking, and the test fails far away with
no message: a Q_ASSERT(file.open(...)) fixture was never created, so four chain
tests timed out against a missing file and the process died on an empty vector.

Tests have QVERIFY/QCOMPARE/QTRY_*, which report and work in every build type.
QVERIFY also returns early, so it is valid in a void helper, which is the one
place Q_ASSERT used to look convenient.

Run from anywhere: ./scripts/check-test-asserts.py [--root DIR]
Exit 0 when clean, 1 with file:line for each use.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

PATTERN = re.compile(r"\bQ_ASSERT(?:_X)?\s*\(")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--root",
        default="tests",
        help="directory to scan (default: tests, relative to the repo root)",
    )
    args = parser.parse_args()

    root = Path(args.root)
    if not root.is_absolute():
        root = Path(__file__).resolve().parent.parent / root
    if not root.is_dir():
        print(f"no such directory: {root}", file=sys.stderr)
        return 2

    findings: list[str] = []
    files = sorted(list(root.rglob("*.cpp")) + list(root.rglob("*.hpp")) + list(root.rglob("*.h")))
    for path in files:
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as error:
            print(f"could not read {path}: {error}", file=sys.stderr)
            continue
        for number, line in enumerate(lines, start=1):
            stripped = line.lstrip()
            if stripped.startswith("//") or stripped.startswith("*"):
                continue
            if PATTERN.search(line):
                findings.append(f"{path}:{number}: Q_ASSERT in a test; use QVERIFY/QCOMPARE")

    if findings:
        for finding in findings:
            print(f"FAIL: {finding}", file=sys.stderr)
        print(f"{len(findings)} finding(s) over {len(files)} test file(s)", file=sys.stderr)
        return 1

    print(f"test-asserts OK - {len(files)} test file(s), no Q_ASSERT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
