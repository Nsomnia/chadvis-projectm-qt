#!/usr/bin/env python3
"""T0330: one source of truth for the sanitizer-annotated test list.

Fails when the canonical list (scripts/safe-tests.txt) disagrees with any of:
  * the set_tests_properties(... ENVIRONMENT ...) block in tests/unit/CMakeLists.txt
  * the RE regex in .github/workflows/tsan.yml
  * the scoped -R in BOTH .github/workflows/asan.yml ctest lanes (T0386)
  * the three hard-gate -R alternations (build/windows/release) agree (T0387)
  * the add_test() entries it names (every canonical name must be a real test)
  * build.sh SAFE_TESTS (must CONTAIN every canonical name; a superset is fine)

Run from anywhere: ./scripts/check-safe-tests.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CANON = ROOT / "scripts" / "safe-tests.txt"
CMAKE = ROOT / "tests" / "unit" / "CMakeLists.txt"
TSAN = ROOT / ".github" / "workflows" / "tsan.yml"
ASAN = ROOT / ".github" / "workflows" / "asan.yml"
GATE_WORKFLOWS = (
    ROOT / ".github" / "workflows" / "build.yml",
    ROOT / ".github" / "workflows" / "windows.yml",
    ROOT / ".github" / "workflows" / "release.yml",
)
BUILDSH = ROOT / "build.sh"


def canonical() -> list[str]:
    names = []
    for line in CANON.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        if s and not s.startswith("#"):
            names.append(s)
    return names


def cmake_annotated() -> list[str]:
    """Names inside the set_tests_properties block whose ENVIRONMENT is TSAN_OPTIONS."""
    text = CMAKE.read_text(encoding="utf-8")
    # Find each set_tests_properties( ... ) call and keep the one that sets
    # ENVIRONMENT "TSAN_OPTIONS". A lazy .*? from the FIRST call in the file
    # would span unrelated blocks (TIMEOUT, other ENVIRONMENTs) and capture
    # their test names too.
    blocks = re.findall(r"set_tests_properties\((.*?)\n\s*\)", text, re.S)
    tsan_blocks = [b for b in blocks if 'ENVIRONMENT' in b and 'TSAN_OPTIONS' in b]
    if len(tsan_blocks) != 1:
        fail(f"expected exactly one TSAN_OPTIONS set_tests_properties block, found {len(tsan_blocks)}")
        return []
    names = re.findall(r"\b(test_[A-Za-z0-9_]+|unit_tests)\b", tsan_blocks[0])
    return list(dict.fromkeys(names))


def workflow_sources_canonical(path: Path, names: list[str], expected_ctest: int) -> bool:
    """A sanitizer workflow must BUILD its RE from scripts/safe-tests.txt and
    pass -R "$RE" to every ctest run (no unfiltered ctest), not hard-code a copy.
    """
    text = path.read_text(encoding="utf-8")
    if "scripts/safe-tests.txt" not in text or not re.search(r"RE=.*\$\{?names", text):
        return False
    # Every ctest COMMAND in the file must carry -R "$RE". Match only real
    # command lines (ctest --test-dir), not the word "ctest" in comments/echo —
    # otherwise prose inflates the count and the guard can never pass.
    n_ctest = len(re.findall(r"^\s*ctest\s+--test-dir", text, re.M))
    n_filtered = len(re.findall(r'-R\s+"?\$RE"?', text))
    if n_ctest < expected_ctest or n_filtered < n_ctest:
        return False
    generated = "|".join(names)
    rx = re.compile(r"^(" + generated + r")$")
    if not all(rx.match(n) for n in names):
        return False
    return rx.match("test_DefinitelyNotInTheList") is None


def tsan_sources_canonical(names: list[str]) -> bool:
    return workflow_sources_canonical(TSAN, names, expected_ctest=1)


def asan_sources_canonical(names: list[str]) -> bool:
    # asan.yml drives TWO lanes (ASan + UBSan), each its own ctest run.
    return workflow_sources_canonical(ASAN, names, expected_ctest=2)


def gate_regexes_agree() -> bool:
    """T0387: the three hard-gate workflows each carry a hand-written headless
    -R alternation. They must be identical (same population) or a test gates on
    one platform and silently does not on another. Extract the alternation from
    each -R '^(...)$' and compare as sets.
    """
    rx = re.compile(r"-R\s+'?\^\(([^']*?)\)\$'?")
    found: list[frozenset] = []
    for path in GATE_WORKFLOWS:
        text = path.read_text(encoding="utf-8")
        m = rx.search(text)
        if not m:
            return False
        found.append(frozenset(m.group(1).split("|")))
    return len(found) == len(GATE_WORKFLOWS) and all(s == found[0] for s in found)


def build_safe() -> list[str]:
    text = BUILDSH.read_text(encoding="utf-8")
    m = re.search(r"SAFE_TESTS=\((.*?)\n\)", text, re.S)
    if not m:
        fail("could not find SAFE_TESTS=(...) in build.sh")
    return re.findall(r"\b(test_[A-Za-z0-9_]+|unit_tests)\b", m.group(1))


def add_test_names() -> set[str]:
    text = CMAKE.read_text(encoding="utf-8")
    return set(re.findall(r"add_test\(\s*NAME\s+([A-Za-z0-9_]+)", text))


ERRORS: list[str] = []


def fail(msg: str) -> None:
    ERRORS.append(msg)


def check_same(label: str, a: list[str], b: list[str]) -> None:
    sa, sb = set(a), set(b)
    if sa == sb:
        return
    missing = sorted(sb - sa)
    extra = sorted(sa - sb)
    fail(f"{label} disagrees with canonical: "
         f"missing={missing or '-'} extra={extra or '-'}")


def main() -> int:
    names = canonical()
    if len(names) != len(set(names)):
        dupes = sorted({n for n in names if names.count(n) > 1})
        fail(f"canonical list has duplicates: {dupes}")

    add = add_test_names()
    unknown = [n for n in names if n not in add]
    if unknown:
        fail(f"canonical names with no add_test(): {unknown}")

    check_same("CMake annotated set", names, cmake_annotated())
    if not tsan_sources_canonical(names):
        fail("tsan.yml does not build a working RE from scripts/safe-tests.txt "
             "(a hard-coded or broken RE would be an unchecked second copy)")
    if not asan_sources_canonical(names):
        fail("asan.yml does not scope BOTH ctest lanes (ASan + UBSan) to the "
             "safe-tests-derived -R RE (T0386): an unfiltered sanitizer ctest "
             "measures a population nobody classified")
    if not gate_regexes_agree():
        fail("build.yml / windows.yml / release.yml headless gate -R regexes "
             "disagree (T0387): a test gates on one platform but not another, "
             "or a -R alternation is missing")

    safe = build_safe()
    missing_in_safe = [n for n in names if n not in safe]
    if missing_in_safe:
        fail(f"build.sh SAFE_TESTS misses canonical entries: {missing_in_safe}")
    if len(safe) != len(set(safe)):
        dupes = sorted({n for n in safe if safe.count(n) > 1})
        fail(f"build.sh SAFE_TESTS has duplicates: {dupes}")

    if ERRORS:
        for e in ERRORS:
            print(f"FAIL: {e}", file=sys.stderr)
        print(f"canonical ({len(names)}): {', '.join(sorted(names))}", file=sys.stderr)
        return 1
    print(f"safe-tests OK — {len(names)} sanitizer-annotated entries, "
          f"all consumers agree, no duplicates")
    return 0


if __name__ == "__main__":
    sys.exit(main())
