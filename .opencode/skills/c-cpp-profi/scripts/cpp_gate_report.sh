#!/usr/bin/env bash
set -u

root="${1:-.}"

if [ ! -d "$root" ]; then
  printf 'error: not a directory: %s\n' "$root" >&2
  exit 2
fi

git_value() {
  key="$1"
  if git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    case "$key" in
      commit) git -C "$root" rev-parse --short HEAD 2>/dev/null || printf 'unknown' ;;
      branch) git -C "$root" branch --show-current 2>/dev/null || printf 'unknown' ;;
      status)
        if [ -z "$(git -C "$root" status --porcelain --untracked-files=normal 2>/dev/null)" ]; then
          printf 'clean'
        else
          printf 'dirty'
        fi
        ;;
      *) printf 'unknown' ;;
    esac
  else
    printf 'not-git'
  fi
}

timestamp="$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || printf 'unknown')"
commit="$(git_value commit)"
branch="$(git_value branch)"
status="$(git_value status)"

cat <<REPORT
# C/C++ Gate Report

- Repo: $root
- Generated UTC: $timestamp
- Git branch: $branch
- Git commit: $commit
- Git status: $status

## Change Scope

- Issue/task:
- Touched files:
- Public API/ABI touched: yes/no
- User-visible rendering/artifacts touched: yes/no
- Parser/input/security boundary touched: yes/no
- Threads/locks/atomics/signals touched: yes/no
- Refactor/simplification claim: yes/no
- Performance claim: yes/no

## Commands

| Gate | Status | Command | Evidence |
|---|---|---|---|
| inventory | not run |  |  |
| format | not applicable |  |  |
| compile | not run |  | include warning-clean: yes or warnings: <count> |
| tests | not run |  |  |
| static analysis | not run |  | include findings: 0 or findings triaged: <summary> |
| ASan+UBSan | not run |  |  |
| TSan/MSan/LSan | not applicable |  |  |
| Helgrind/DRD/rr/stress | not applicable |  |  |
| fuzz/corpus | not applicable |  |  |
| performance | not applicable |  | include baseline:, profile:/hotspot:, score:/opportunity:, oracle:/isomorphism:, after:/result: |
| portability | not applicable |  |  |
| ABI/API | not applicable |  |  |
| refactor isomorphism | not applicable |  |  |
| differential oracle | not applicable |  | include origin-triple:, target-triple:, emulator:/hardware:, corpus: |
| migration ledger | not applicable |  | include caller-census:, ledger: |
| golden artifacts | not applicable |  |  |
| idea card | not applicable |  | include idea-card: validated by cpp_idea_check.py; kind: <accretive\|radical>; score: <n> |
| comprehension | not applicable |  | include entry-point:, module-map:, callgraph:, intent: |

Use statuses: passed, failed, not run, not applicable.

## ABI/API Evidence

- Supported contract:
- Old artifact/header:
- New artifact/header:
- Tooling:
- Symbol/layout/API result:
- Downstream compile/run result:
- Intentional breaks:

## Refactor Isomorphism Evidence

- Baseline command/artifacts:
- Callsite census:
- Opportunity score:
- One lever:
- Behavior axes checked:
- ABI/API/layout result:
- Ownership/RAII/exception-safety result:
- Template/concept/ODR/build-system result:
- Concurrency/reentrancy result:
- Performance hot-path result:
- Before/after LOC and warning counts:
- Rejection log:

## Golden Artifact Evidence

- Surface:
- Matrix:
- Baseline:
- Candidate:
- Diff command:
- Threshold:
- Result:
- Accepted artifact path:

## Performance Evidence

- Benchmark command:
- Environment:
- Baseline:
- Candidate:
- Profile/hotspot:
- Result:

## Concurrency Evidence

- Thread/ownership map:
- Lock graph:
- Concrete interleavings:
- Atomic memory-order proof:
- Condition-variable predicate:
- Signal/fork/loader surfaces:
- Callback/FFI/allocator reentrancy:
- Dynamic/stress gates:

## Residual Risk

- Missing gates:
- Why missing gates are acceptable or follow-up issue:
- Follow-up issues:

## Evidence Checker

Run before claiming completion:

\`\`\`bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py <this-report.md> --profile basic
\`\`\`

Add risk profiles for the touched surface: parser, memory, security, public-abi,
performance, concurrency, refactor, native-ui, portability.

For release, security, parser, memory, or review work, also require explicit
compile-warning and analyzer-output claims:

\`\`\`bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py <this-report.md> --profile basic --require-warning-clean --require-analyzer-review
\`\`\`

For optimization claims, require a self-contained performance proof:

\`\`\`bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py <this-report.md> --profile performance --require-performance-proof
\`\`\`
REPORT
