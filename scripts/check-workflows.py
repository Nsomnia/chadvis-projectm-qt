#!/usr/bin/env python3
"""Reject workflow files GitHub will refuse to load.

Why this exists: `yaml.safe_load` -- and therefore every "validate the YAML"
habit -- silently keeps the LAST of two duplicate keys in a mapping. GitHub's
parser does not: it rejects the entire file, and the run it produces has
**zero jobs** with the useless summary "This run likely failed because of a
workflow file issue". Nothing local notices, because nothing local parses the
way GitHub does.

That is exactly how `.github/workflows/build.yml` came to carry two `modules:`
keys inside one `with:` block. A second `modules:` was appended to a Qt step
that already had one; PyYAML said fine; the required gate on `main` went dark
for three commits while `ctest`, `build.sh` and every other local check stayed
green.

So this script does not use a YAML library at all. It is a deliberately small
indentation-aware scanner that knows the two things that matter here:

  * a mapping may not repeat a key at the same indentation level;
  * block scalars (`run: |`, `with: >-`) are opaque text and are skipped, so a
    shell body full of `foo: bar` never produces a false positive.

It also flags tabs used for indentation (illegal in YAML) and a workflow with no
`jobs:` key (parses, then silently does nothing).

Usage:
    ./scripts/check-workflows.py                 # every .github/workflows/*.yml
    ./scripts/check-workflows.py FILE [FILE...]  # just these

Exit status: 0 clean, 1 problems found.
No third-party dependencies -- any python3 on this box will do.
"""

from __future__ import annotations

import glob
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# `key:` with an optional value. Keys in workflows are identifiers, numbers or
# quoted strings; anything else at line start is prose or a scalar continuation.
KEY_RE = re.compile(r"^(?P<indent> *)(?P<key>[^\s#][^:]*?):(?P<rest>.*)$")
BLOCK_SCALAR_RE = re.compile(r"^[|>][+-]?\d*$")


def scan(path: Path) -> list[str]:
    """Return a list of human-readable problems; empty means the file is sane."""
    problems: list[str] = []
    lines = path.read_text(encoding="utf-8").splitlines()

    # Stack of (indent, {key: line_number}) for the mappings currently open.
    stack: list[tuple[int, dict[str, int]]] = []
    block_scalar_indent: int | None = None
    has_jobs = False
    in_document = True

    for number, raw in enumerate(lines, start=1):
        if "\t" in raw:
            problems.append(f"line {number}: literal tab (YAML forbids tabs for indentation)")
            raw = raw.replace("\t", "    ")

        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped in ("---", "..."):
            stack.clear()
            block_scalar_indent = None
            in_document = stripped == "---"
            continue

        indent = len(raw) - len(raw.lstrip(" "))

        # Inside a block scalar, everything more indented than its parent is text.
        if block_scalar_indent is not None:
            if indent > block_scalar_indent:
                continue
            block_scalar_indent = None

        if stripped.startswith("- "):
            # A sequence item opens its own mapping scope at this indentation.
            while stack and stack[-1][0] > indent:
                stack.pop()
            if stack and stack[-1][0] == indent:
                stack.pop()
            continue

        match = KEY_RE.match(raw)
        if not match:
            continue

        while stack and stack[-1][0] > indent:
            stack.pop()
        if not stack or stack[-1][0] < indent:
            stack.append((indent, {}))
        keys = stack[-1][1]

        key = match.group("key").strip().strip("'\"")
        if key in keys:
            problems.append(
                f"line {number}: duplicate key {key!r} "
                f"(first defined on line {keys[key]}) -- GitHub rejects the whole file"
            )
        else:
            keys[key] = number

        if indent == 0 and key == "jobs":
            has_jobs = True

        rest = match.group("rest").strip()
        if rest and BLOCK_SCALAR_RE.match(rest):
            block_scalar_indent = indent

    if in_document and not has_jobs:
        problems.append("no top-level 'jobs:' key -- the file parses but does nothing")

    return problems


def main(argv: list[str]) -> int:
    if argv:
        paths = [Path(a) for a in argv]
    else:
        paths = sorted(Path(p) for p in glob.glob(str(REPO_ROOT / ".github/workflows/*.yml")))
    if not paths:
        print("check-workflows: no workflow files found", file=sys.stderr)
        return 1

    failed = 0
    for path in paths:
        problems = scan(path)
        if problems:
            failed += 1
            print(f"FAIL {path}")
            for problem in problems:
                print(f"     {problem}")
        else:
            print(f"ok   {path}")
    if failed:
        print(f"\n{failed} workflow file(s) GitHub would reject.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
