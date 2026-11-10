#!/usr/bin/env python3
"""T0394: report SunoEndpoints constants with no call site, and pin the count.

Every `constexpr std::string_view NAME` in src/suno/SunoEndpoints.hpp is a
captured route. A NAME that is never referenced as `endpoints::NAME` anywhere
in src/ or tests/ is a constant that has drifted out of the inventory: either
a route nobody wired, or a name the inventory still lists as live.

The repo already carries a large, known dead set (see TODO T0346). Failing on
that today would make the gate dead on arrival. Instead the count is pinned:
--baseline N fails when the dead count CHANGES from N, so adding a new unused
constant (or wiring one without updating the baseline) is caught, while the
existing debt is grandfathered until it is worked down deliberately.

Run from anywhere:
  ./scripts/check-endpoints.py                 # report, exit 0
  ./scripts/check-endpoints.py --baseline 127 # pin; exit 1 on drift
  ./scripts/check-endpoints.py --list         # print dead names
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HEADER = "src/suno/SunoEndpoints.hpp"
CONST = re.compile(
    r"constexpr\s+std::string_view\s+([A-Za-z_][A-Za-z0-9_]*)\s*="
)
# A use is `endpoints::NAME` (the namespace the header declares). A bare NAME
# is not counted: it is too easy to match a local with the same spelling.
USE_TMPL = r"\bendpoints::"
EXTS = (".cpp", ".hpp", ".h", ".cc", ".cxx")


def constants(repo: Path) -> list[str]:
    text = (repo / HEADER).read_text(encoding="utf-8", errors="replace")
    return CONST.findall(text)


def used_names(repo: Path, names: list[str]) -> set[str]:
    # Build one alternation so each file is scanned once.
    pat = re.compile(USE_TMPL + r"(" + "|".join(re.escape(n) for n in names) + r")\b")
    used: set[str] = set()
    for base in ("src", "tests"):
        root = repo / base
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if path.suffix not in EXTS or not path.is_file():
                continue
            if path.resolve() == (repo / HEADER).resolve():
                continue  # definitions are not uses
            text = path.read_text(encoding="utf-8", errors="replace")
            for m in pat.finditer(text):
                used.add(m.group(1))
    return used


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline", type=int, default=None,
                    help="fail when the dead-constant count differs from N")
    ap.add_argument("--list", action="store_true",
                    help="print each dead constant name")
    args = ap.parse_args()

    repo = Path(__file__).resolve().parent.parent
    names = constants(repo)
    used = used_names(repo, names)
    dead = sorted(set(names) - used)

    print(f"endpoints: {len(names)} constants, {len(used)} referenced, "
          f"{len(dead)} with zero call sites")
    if args.list:
        for name in dead:
            print(f"  {name}")

    if args.baseline is not None and len(dead) != args.baseline:
        print(f"FAIL: dead-constant count {len(dead)} != baseline {args.baseline} "
              f"— a new unused endpoint was added, or one was wired without "
              f"updating the baseline. Re-run --list to see the set.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
