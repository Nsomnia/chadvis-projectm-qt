#!/usr/bin/env python3
"""T0444: fail when the integration binary is older than its sources.

``integration_tests`` is deliberately OUTSIDE build.sh's ``--tests`` build
list (the GL / burn-in / audio lanes skip headless, so "--safe" can never be
evidence for them), so nothing rebuilds it on the everyday loop. That is how
T0171's new GL slot landed in a binary that was never rebuilt: edit the
sources, run --tests, get green, and the new slot never ran.

This gate needs no build and answers in milliseconds. For every profile
build directory that exists, it compares the integration_tests binary mtime
against every file under tests/integration/ (sources AND CMakeLists):
a newer source means the binary a developer would run never saw the edit.
It also pins the exclusion itself -- if integration_tests ever lands in
build.sh SAFE_TESTS, --tests starts paying its link cost and the design
above is gone, so that is a finding too.

Run from anywhere: ./scripts/check-integration-fresh.py [--root DIR]
Exit 0 when every existing binary is fresh, 1 with one line per stale dir.
A machine with no build directory (CI) passes: there is nothing stale.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Profile dir -> build.sh flags that rebuild that profile. build-fast is the
# default, so its flag list is empty.
PROFILES: dict[str, list[str]] = {
    "build-fast": [],
    "build": ["--release"],
    "build-debug": ["--debug"],
    "build-tsan": ["--tsan"],
    "build-asan": ["--asan"],
    "build-ubsan": ["--ubsan"],
}
BINARY_REL = Path("tests/integration/integration_tests")
SOURCE_DIR = Path("tests/integration")


def build_sh_safe_targets(build_sh: Path) -> list[str]:
    """The --tests build target list, or [] when it cannot be read."""
    try:
        text = build_sh.read_text(encoding="utf-8")
    except OSError:
        return []
    m = re.search(r"SAFE_TESTS=\((.*?)\n\)", text, re.S)
    if not m:
        return []
    return re.findall(r"\b(test_[A-Za-z0-9_]+|unit_tests|integration_tests)\b", m.group(1))


def binary_for(build_dir: Path) -> Path | None:
    for name in ("integration_tests", "integration_tests.exe"):
        candidate = build_dir / BINARY_REL.with_name(name)
        if candidate.exists():
            return candidate
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--root",
        default=".",
        help="repository root (default: the tree this script lives in)",
    )
    args = parser.parse_args()

    root = Path(args.root)
    if not root.is_absolute():
        root = Path(__file__).resolve().parent.parent if args.root == "." else (Path.cwd() / root)
    source_dir = root / SOURCE_DIR
    if not source_dir.is_dir():
        print(f"no such directory: {source_dir}", file=sys.stderr)
        return 2
    sources = sorted(p for p in source_dir.rglob("*") if p.is_file())
    if not sources:
        print(f"no source files under {source_dir}", file=sys.stderr)
        return 2

    findings: list[str] = []

    # Pin the exclusion: the --tests loop must not pay for integration_tests.
    safe = build_sh_safe_targets(root / "build.sh")
    if "integration_tests" in safe:
        findings.append(
            "build.sh: integration_tests is in SAFE_TESTS; --tests would build it on "
            "every loop, but it is deliberately excluded (GL/audio lanes)."
        )

    fresh = stale = not_built = 0
    for dirname, flags in PROFILES.items():
        build_dir = root / dirname
        if not build_dir.is_dir():
            continue
        binary = binary_for(build_dir)
        if binary is None:
            not_built += 1
            continue
        bin_mtime = binary.stat().st_mtime_ns
        newest = max(sources, key=lambda p: p.stat().st_mtime_ns)
        if newest.stat().st_mtime_ns > bin_mtime:
            stale += 1
            flags_str = (" " + " ".join(flags)) if flags else ""
            findings.append(
                f"{binary.relative_to(root)} is older than {newest.relative_to(root)} -- "
                f"rebuild: ./build.sh{flags_str} --target integration_tests"
            )
        else:
            fresh += 1

    if findings:
        for finding in findings:
            print(f"FAIL: {finding}", file=sys.stderr)
        print(f"integration-fresh: {stale} stale, {fresh} fresh, {not_built} not built",
              file=sys.stderr)
        return 1

    print(f"integration-fresh OK - {fresh} integration binary(ies) fresh "
          f"against {len(sources)} source(s), {not_built} not built")
    return 0


if __name__ == "__main__":
    sys.exit(main())
