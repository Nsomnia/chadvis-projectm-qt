#!/usr/bin/env bash
#
# c-cpp-profi pre-commit hook (portable template)
#
# Installs as a git pre-commit hook in a consumer repo. It runs the skill's
# read-only risk scan over the staged C/C++ files and prints a reminder to fill
# and --derive-profiles-validate a gate report before handoff.
#
# Install (from the repo root, with the skill vendored at .opencode/skills/c-cpp-profi/):
#   ln -s ../../.opencode/skills/c-cpp-profi/assets/ci/pre-commit-c-cpp-profi.sh .git/hooks/pre-commit
# or copy it:
#   cp .opencode/skills/c-cpp-profi/assets/ci/pre-commit-c-cpp-profi.sh .git/hooks/pre-commit
#   chmod +x .git/hooks/pre-commit
#
# Locate the skill: by default this hook looks for the skill at
# .opencode/skills/c-cpp-profi relative to the repo root. Override with the CPP_PROFI_DIR
# env var, e.g. in .git/hooks/pre-commit or your shell:
#   CPP_PROFI_DIR=vendor/c-cpp-profi git commit ...
#
# Exit policy: this hook is ADVISORY. The risk scan's findings (raw strcpy,
# malloc, casts, threading primitives, etc.) are triage prompts, not commit
# blockers, so a clean scan and a scan with findings BOTH exit 0. The hook only
# exits non-zero on a HARD ERROR: the configured skill directory or the risk-scan
# script is missing, or rg is not installed (the scan cannot run at all). This
# keeps the hook from silently passing while doing nothing.

set -eu

CPP_PROFI_DIR="${CPP_PROFI_DIR:-.opencode/skills/c-cpp-profi}"
RISK_SCAN="${CPP_PROFI_DIR}/scripts/cpp_risk_scan.sh"

# Collect staged C/C++ sources/headers (Added/Copied/Modified/Renamed only).
staged="$(git diff --cached --name-only --diff-filter=ACMR \
  | grep -E '\.(c|cc|cpp|cxx|h|hpp)$' || true)"

if [ -z "$staged" ]; then
  # No C/C++ changes staged: nothing for this hook to do.
  exit 0
fi

# Hard error: the skill must be present to scan anything.
if [ ! -f "$RISK_SCAN" ]; then
  echo "c-cpp-profi pre-commit: risk-scan script not found at ${RISK_SCAN}" >&2
  echo "  vendor the skill at .opencode/skills/c-cpp-profi/ or set CPP_PROFI_DIR to its location" >&2
  exit 1
fi

# Hard error: rg is required by cpp_risk_scan.sh.
if ! command -v rg >/dev/null 2>&1; then
  echo "c-cpp-profi pre-commit: rg (ripgrep) is required by cpp_risk_scan.sh but not found" >&2
  exit 1
fi

echo "c-cpp-profi pre-commit: scanning staged C/C++ files"

# Scan only the staged paths. cpp_risk_scan.sh exits 1 when it finds advisory
# matches and 0 when clean; either is fine here, so we never let its advisory
# exit code abort the commit (set -e would otherwise stop us).
scan_paths=""
while IFS= read -r path; do
  [ -n "$path" ] || continue
  if [ -e "$path" ]; then
    scan_paths="${scan_paths} ${path}"
  fi
done <<EOF
$staged
EOF

if [ -n "$scan_paths" ]; then
  # shellcheck disable=SC2086
  bash "$RISK_SCAN" $scan_paths || true
fi

cat <<'REMINDER'

c-cpp-profi reminder: the scan above is advisory triage, not a verdict.
Before handoff, fill a gate report and validate it:
  bash CPP_PROFI_DIR/scripts/cpp_gate_report.sh . > gate-report.md
  # fill in the Change Scope answers (yes/no) and every gate row, then:
  python3 CPP_PROFI_DIR/scripts/cpp_evidence_check.py gate-report.md --derive-profiles
(substitute CPP_PROFI_DIR with your skill path; default .opencode/skills/c-cpp-profi)
REMINDER

exit 0
