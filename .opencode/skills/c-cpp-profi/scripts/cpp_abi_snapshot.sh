#!/usr/bin/env bash
set -u

usage() {
  cat <<'USAGE'
usage: cpp_abi_snapshot.sh <candidate-library-or-object> [baseline-library-or-object]

Print a Markdown ABI/API evidence packet for ELF libraries or objects.

This helper is intentionally read-only with respect to the input artifacts. It
does not create temporary files or modify the target repo. Redirect stdout if a
project wants to store the snapshot.

When a baseline is provided, the helper compares exported symbol names with the
candidate. This is a fallback smoke gate, not a replacement for libabigail
abidiff or equivalent type/layout-aware ABI comparison.
USAGE
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

candidate="${1:-}"
baseline="${2:-}"

if [ -z "$candidate" ]; then
  usage >&2
  exit 2
fi

if [ ! -f "$candidate" ]; then
  printf 'error: candidate artifact is not a file: %s\n' "$candidate" >&2
  exit 2
fi

if [ -n "$baseline" ] && [ ! -f "$baseline" ]; then
  printf 'error: baseline artifact is not a file: %s\n' "$baseline" >&2
  exit 2
fi

has_tool() {
  command -v "$1" >/dev/null 2>&1
}

tool_path() {
  if has_tool "$1"; then
    command -v "$1"
  else
    printf 'missing'
  fi
}

demangle() {
  if has_tool c++filt; then
    c++filt
  else
    cat
  fi
}

symbol_list() {
  artifact="$1"
  if ! has_tool nm; then
    return 127
  fi
  nm -D --defined-only "$artifact" 2>/dev/null | awk '{print $NF}' | demangle | LC_ALL=C sort -u
}

print_command_block() {
  label="$1"
  shift
  printf '\n### %s\n\n' "$label"
  printf '```text\n'
  "$@" 2>&1 || printf 'command failed: %s\n' "$*"
  printf '```\n'
}

print_artifact_snapshot() {
  label="$1"
  artifact="$2"

  printf '\n## %s\n\n' "$label"
  printf '%s\n' "- Artifact: \`$artifact\`"

  if has_tool file; then
    print_command_block "file" file "$artifact"
  fi

  if has_tool readelf; then
    print_command_block "ELF header" readelf -h "$artifact"
    print_command_block "Dynamic section" readelf -d "$artifact"
    print_command_block "Dynamic symbols" readelf -Ws --wide "$artifact"
  else
    printf '\nnot run: readelf unavailable; cannot inspect ELF header, dynamic section, or dynamic symbol table.\n'
  fi

  if has_tool nm; then
    print_command_block "Defined exported symbol names" symbol_list "$artifact"
  else
    printf '\nnot run: nm unavailable; cannot list exported symbols.\n'
  fi

  if has_tool objdump; then
    print_command_block "Dynamic symbol table" objdump -T "$artifact"
  else
    printf '\nnot run: objdump unavailable; cannot cross-check dynamic symbols.\n'
  fi
}

cat <<REPORT
# ABI/API Snapshot

- Candidate: \`$candidate\`
- Baseline: \`${baseline:-not provided}\`
- Tool paths:
  - readelf: \`$(tool_path readelf)\`
  - objdump: \`$(tool_path objdump)\`
  - nm: \`$(tool_path nm)\`
  - c++filt: \`$(tool_path c++filt)\`
  - abidiff: \`$(tool_path abidiff)\`
  - abi-dumper: \`$(tool_path abi-dumper)\`
  - abi-compliance-checker: \`$(tool_path abi-compliance-checker)\`
  - pahole: \`$(tool_path pahole)\`

## Interpretation Contract

- passed: exact command ran and the reviewed output supports the claimed ABI/API state.
- failed: exact command ran and produced an unhandled ABI/API delta or command failure.
- not run: relevant richer tool is unavailable or the artifact lacks needed metadata.
- This packet checks exported symbols and ELF metadata. It does not prove C++ class layout, vtable layout, parameter type compatibility, inline/template API stability, exception ABI, allocator ownership, or semantic compatibility.
REPORT

print_artifact_snapshot "Candidate Snapshot" "$candidate"

if [ -n "$baseline" ]; then
  print_artifact_snapshot "Baseline Snapshot" "$baseline"

  printf '\n## Exported Symbol Name Diff\n\n'
  if has_tool nm; then
    if diff -u <(symbol_list "$baseline") <(symbol_list "$candidate"); then
      printf '\nResult: no exported symbol name delta detected by nm -D --defined-only | c++filt | sort -u.\n'
    else
      printf '\nResult: exported symbol name delta detected; review every added, removed, or renamed symbol before claiming ABI compatibility.\n'
    fi
  else
    printf 'not run: nm unavailable; cannot compare exported symbol names.\n'
  fi

  if has_tool abidiff; then
    print_command_block "libabigail abidiff" abidiff "$baseline" "$candidate"
  else
    printf '\nnot run: abidiff unavailable; symbol diff is only fallback evidence.\n'
  fi
fi
