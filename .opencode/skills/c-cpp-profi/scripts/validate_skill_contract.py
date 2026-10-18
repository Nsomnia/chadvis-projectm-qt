#!/usr/bin/env python3
"""Validate the c-cpp-profi skill contract.

This harness is intentionally dependency-free. It checks packaging, required
references, examples, assets, script syntax, and local Markdown links.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess  # nosec B404 - this only invokes fixed local syntax checks.
import sys
from pathlib import Path


REQUIRED_REFERENCES = [
    "AGENT-OPERATING-MODE.md",
    "BUILD-PORTABILITY.md",
    "C-CPP-EXPERT-CANON.md",
    "CODE-TRANSFORM.md",
    "CONCURRENCY-DEADLOCKS.md",
    "DESIGN-PARADIGMS.md",
    "DOCUMENTATION.md",
    "DOMAIN-AGNOSTIC-MASTERY.md",
    "INNOVATION-ENGINE.md",
    "LANGUAGE-INTERNALS.md",
    "MEMORY-SAFETY.md",
    "NATIVE-UI-GOLDENS.md",
    "PERFORMANCE.md",
    "QUALITY-GATES.md",
    "REFACTOR-ISOMORPHISM.md",
    "REMEDIATION-RECIPES.md",
    "REPO-COMPREHENSION.md",
    "SECURITY-REVIEW.md",
    "STANDARDS-VERSIONS-IDIOMS.md",
    "TESTING-FUZZING.md",
    "TOOLCHAIN-MATRIX.md",
    "TOOLCHAIN-TEMPLATES.md",
    "UNKNOWN-DOMAIN.md",
]

REQUIRED_SCRIPTS = [
    "cpp_abi_snapshot.sh",
    "cpp_backlog.sh",
    "cpp_comprehension_map.sh",
    "cpp_docs_check.py",
    "cpp_domain_detect.sh",
    "cpp_evidence_check.py",
    "cpp_gate_plan.sh",
    "cpp_idea_check.py",
    "cpp_gate_report.sh",
    "cpp_inventory.sh",
    "cpp_pixel_diff.py",
    "cpp_risk_scan.sh",
    "validate_skill_contract.py",
]

REQUIRED_ASSETS = [
    "assets/ci/README.md",
    "assets/ci/github-actions-c-cpp-profi.yml",
    "assets/ci/pre-commit-c-cpp-profi.sh",
    "assets/cmake/CMakePresets.sanitizers.json",
    "assets/cmake/cmake/CppSystemsSanitizers.cmake",
    "assets/cmake/fuzz/CMakeLists.libfuzzer.example.txt",
    "assets/fuzz/libfuzzer_target.c",
    "assets/fuzz/libfuzzer_target.cc",
    "assets/meson/fuzz/meson.build.libfuzzer.example",
    "assets/meson/native/asan-ubsan.ini",
    "assets/meson/native/tsan.ini",
]

REQUIRED_EXAMPLES = [
    "c-library.md",
    "modern-cpp-library.md",
    "embedded-c.md",
    "parser-input.md",
    "native-ui-rendering.md",
    "idea-generation.md",
    "code-transform.md",
    "documentation.md",
    "domain-pack.md",
    "remediation.md",
]

REQUIRED_SKILL_TEXT = [
    "c-cpp-profi",
    "C-CPP-EXPERT-CANON.md",
    "DOMAIN-AGNOSTIC-MASTERY.md",
    "UNKNOWN-DOMAIN.md",
    "INNOVATION-ENGINE.md",
    "TOOLCHAIN-MATRIX.md",
    "QUALITY-GATES.md",
    "CONCURRENCY-DEADLOCKS.md",
    "REFACTOR-ISOMORPHISM.md",
    "REMEDIATION-RECIPES.md",
    "CODE-TRANSFORM.md",
    "DOCUMENTATION.md",
    "REPO-COMPREHENSION.md",
    "examples/c-library.md",
    "examples/idea-generation.md",
    "examples/code-transform.md",
    "examples/documentation.md",
    "examples/domain-pack.md",
    "examples/remediation.md",
    "cpp_docs_check.py",
    "cpp_evidence_check.py",
    "cpp_idea_check.py",
    "validate_skill_contract.py",
]


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def parse_frontmatter(text: str) -> dict[str, str] | None:
    # G23: tolerate CRLF (Windows-authored) and a closing fence that is the final
    # line with no trailing newline.
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    match = re.match(r"^---\n(.*?)\n---\s*(?:\n|$)", text, flags=re.S)
    if not match:
        return None
    data: dict[str, str] = {}
    for raw_line in match.group(1).splitlines():
        if not raw_line.strip():
            continue
        if ":" not in raw_line:
            return None
        key, value = raw_line.split(":", 1)
        data[key.strip()] = value.strip().strip('"').strip("'")
    return data


def check_markdown_links(skill_dir: Path, errors: list[str]) -> None:
    files = [skill_dir / "SKILL.md"]
    files.extend(sorted((skill_dir / "references").glob("*.md")))
    files.extend(sorted((skill_dir / "examples").glob("*.md")))

    link_re = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")

    def strip_code(text: str) -> str:
        # Code spans/blocks are NOT parsed for Markdown links, so a C++ lambda or
        # subscript-call written in backticks (e.g. `[capture](params)`, `a[i](x)`)
        # is not a broken link. Remove fenced blocks and inline spans before scanning.
        text = re.sub(r"```.*?```", "", text, flags=re.DOTALL)
        text = re.sub(r"~~~.*?~~~", "", text, flags=re.DOTALL)
        text = re.sub(r"`+[^`]*`+", "", text)
        return text

    for file_path in files:
        if not file_path.exists():
            continue
        for target in link_re.findall(strip_code(read(file_path))):
            target = target.strip()
            if not target or target.startswith("#"):
                continue
            if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
                continue
            target = target.split("#", 1)[0].strip()
            if " " in target and not target.startswith("<"):
                target = target.split(" ", 1)[0]
            target = target.strip("<>")
            if not target:
                continue
            resolved = (file_path.parent / target).resolve()
            try:
                resolved.relative_to(skill_dir.resolve())
            except ValueError:
                continue
            if not resolved.exists():
                rel_file = file_path.relative_to(skill_dir)
                fail(errors, f"{rel_file}: broken Markdown link to {target}")


def run_bash_syntax(skill_dir: Path, errors: list[str]) -> None:
    bash = shutil.which("bash")
    if bash is None:
        fail(errors, "bash not found; cannot syntax-check shell helpers")
        return
    scripts_dir = skill_dir / "scripts"
    if not scripts_dir.is_dir():
        fail(errors, "scripts/ directory missing; cannot syntax-check shell helpers")
        return
    # G4: `bash -n a.sh b.sh ...` parses only the FIRST arg as a script (the rest
    # become $0,$1,...), so 7 of 8 helpers went unchecked -> false PASS. Check each
    # file individually and accumulate failures, like run_python_syntax does.
    for name in REQUIRED_SCRIPTS:
        if not name.endswith(".sh"):
            continue
        script = scripts_dir / name
        if not script.exists():
            continue  # a missing required script is already reported in main()
        try:
            result = subprocess.run(
                [bash, "-n", str(script)],
                cwd=str(scripts_dir),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
                timeout=15,
            )  # nosec B603 - shell script paths come from the skill contract constants.
        except (FileNotFoundError, OSError) as exc:
            fail(errors, f"bash -n could not run on {name}: {exc}")
            continue
        if result.returncode != 0:
            fail(errors, f"bash -n failed for {name}:\n" + result.stdout)


def run_python_syntax(skill_dir: Path, errors: list[str]) -> None:
    scripts = [skill_dir / "scripts" / name for name in REQUIRED_SCRIPTS if name.endswith(".py")]
    for script in scripts:
        if not script.exists():
            continue  # a missing required script is already reported in main()
        try:
            compile(read(script), str(script), "exec")
        except SyntaxError as exc:
            fail(errors, f"{script.relative_to(skill_dir)}: Python syntax error: {exc}")


def main(argv: list[str]) -> int:
    # G16: real argument parsing + --help, and a clean FAIL (not a traceback) when
    # the target directory does not exist or is not a directory.
    parser = argparse.ArgumentParser(
        prog="validate_skill_contract.py",
        description="Validate the c-cpp-profi skill contract (packaging, references, syntax, links).",
    )
    parser.add_argument(
        "skill_dir", nargs="?", default=".",
        help="path to the skill directory to validate (default: current directory)",
    )
    args = parser.parse_args(argv[1:])
    skill_dir = Path(args.skill_dir).resolve()
    errors: list[str] = []

    if not skill_dir.is_dir():
        print("c-cpp-profi contract: FAIL")
        print(f"- skill directory not found: {args.skill_dir}")
        return 1

    skill_md = skill_dir / "SKILL.md"
    if not skill_md.exists():
        fail(errors, "SKILL.md missing")
    else:
        skill_text = read(skill_md)
        frontmatter = parse_frontmatter(skill_text)
        if frontmatter is None:
            fail(errors, "SKILL.md frontmatter missing or invalid")
        else:
            if set(frontmatter) != {"name", "description"}:
                fail(errors, "SKILL.md frontmatter must contain only name and description")
            if frontmatter.get("name") != "c-cpp-profi":
                fail(errors, "SKILL.md name must be c-cpp-profi")
            if "C++" not in frontmatter.get("description", ""):
                fail(errors, "SKILL.md description should mention C++")
        for needle in REQUIRED_SKILL_TEXT:
            if needle not in skill_text:
                fail(errors, f"SKILL.md does not reference {needle}")
        if re.search(r"\b(TODO|FIXME|CUSTOMIZE)\b", skill_text):
            fail(errors, "SKILL.md contains template placeholder text")

    if not (skill_dir / "agents" / "openai.yaml").exists():
        fail(errors, "agents/openai.yaml missing")

    for name in REQUIRED_REFERENCES:
        if not (skill_dir / "references" / name).exists():
            fail(errors, f"required reference missing: {name}")

    for name in REQUIRED_SCRIPTS:
        if not (skill_dir / "scripts" / name).exists():
            fail(errors, f"required script missing: {name}")

    for name in REQUIRED_ASSETS:
        if not (skill_dir / name).exists():
            fail(errors, f"required asset missing: {name}")

    for name in REQUIRED_EXAMPLES:
        path = skill_dir / "examples" / name
        if not path.exists():
            fail(errors, f"required example missing: {name}")
        elif "Evidence Packet" not in read(path):
            fail(errors, f"example lacks Evidence Packet section: {name}")

    check_markdown_links(skill_dir, errors)
    run_bash_syntax(skill_dir, errors)
    run_python_syntax(skill_dir, errors)

    if errors:
        print("c-cpp-profi contract: FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print("c-cpp-profi contract: PASS")
    print(f"references={len(REQUIRED_REFERENCES)} examples={len(REQUIRED_EXAMPLES)} assets={len(REQUIRED_ASSETS)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
