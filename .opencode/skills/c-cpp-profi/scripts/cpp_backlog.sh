#!/usr/bin/env bash
#
# cpp_backlog.sh - derive a deduplicated capability-gap backlog for a C/C++ repo.
#
# This is the "Enumerate Before Inventing" tool from references/INNOVATION-ENGINE.md.
# It is READ-ONLY: it never writes to the target repo. It runs cpp_inventory.sh and
# cpp_risk_scan.sh (located next to this script), plus its own deterministic greps,
# and emits one capability gap per line across four fixed lanes that hold for any
# C/C++ repo:
#
#   hardening           - missing sanitizer/_FORTIFY_SOURCE/stack-protector/CFI build
#                         evidence; raw strcpy/strcat/sprintf/gets/unchecked malloc(*
#   api-ergonomics      - pointer+length signatures with no span/view; owning raw
#                         new/malloc crossing a header boundary
#   portability         - only one compiler/arch/std exercised; load-bearing
#                         long/time_t/endian/packing assumptions
#   test-fuzz-coverage  - parse/decode entry points (incl. const uint8_t*/size_t)
#                         with no fuzz harness referencing them
#
# Every emitted row carries an evidence anchor (repo-relative file:line, or an
# inventory key). A row with no anchor is never emitted. Output is deterministic:
# repo-relative paths only, LC_ALL=C sort, no timestamps, no $RANDOM, no absolute
# paths, so two runs on an unchanged tree byte-match.
#
# Usage:
#   cpp_backlog.sh [REPO] [--json]     (default REPO=.)
#   cpp_backlog.sh --self-test
#
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<'USAGE'
usage: cpp_backlog.sh [REPO] [--json]
       cpp_backlog.sh --self-test

Emits a deduplicated capability-gap backlog (lane | gap | anchor) for a C/C++
repo. READ-ONLY. Deterministic and reproducible: two runs byte-match.
Anchors are repo-relative file:line or an inventory key.
USAGE
}

# ---------------------------------------------------------------------------
# rg search base, mirroring cpp_risk_scan.sh's exclusions so anchors line up.
# ---------------------------------------------------------------------------
rg_available() {
  command -v rg >/dev/null 2>&1
}

# Shared exclusion globs: vendored/build trees AND non-shipped dirs, mirroring
# cpp_risk_scan.sh so anchors line up across the helper family. Path-segment globs
# alone miss suffix-named tests (leveldb db_test.cc, re2 parse_test.cc), the
# `testing/` gerund (re2), `extras/` (miniaudio split-copy + vendored decoders),
# and flat-root harnesses (lua ltests.*) — all added below (R3).
EXCLUDE_GLOBS=(
  --glob '!**/.git/**'
  --glob '!**/build/**'
  --glob '!**/_deps/**'
  --glob '!**/third_party/**'
  --glob '!**/thirdparty/**'
  --glob '!**/vendor/**'
  --glob '!**/extern/**'
  --glob '!**/external/**'
  --glob '!**/tests/**'
  --glob '!**/test/**'
  --glob '!**/testing/**'
  --glob '!**/bench/**'
  --glob '!**/benches/**'
  --glob '!**/benchmark/**'
  --glob '!**/benchmarks/**'
  --glob '!**/examples/**'
  --glob '!**/example/**'
  --glob '!**/tutorials/**'
  --glob '!**/tutorial/**'
  --glob '!**/extras/**'
  --glob '!**/extra/**'
  --glob '!**/docs/**'
  --glob '!**/doc/**'
  # R10: vendored *runtime* deps + generated amalgam/aux trees the
  # `_deps/`/`third_party/`/`vendor/` set missed (kept identical to the sibling
  # scripts): bare `deps/` (redis bundles), `dependencies/` (simdjson bench deps),
  # `singleheader/` generated amalgam (simdjson), `autosetup/` + the `jimsh0.c`
  # bootstrap amalgam (sqlite), and OSS-Fuzz `fuzz/`/`fuzzing/` harness dirs. The
  # test-fuzz-coverage lane still discovers harnesses via its own explicit
  # `**/fuzz/**` / `**/fuzzing/**` globs, so coverage detection is unaffected.
  --glob '!**/deps/**'
  --glob '!**/dependencies/**'
  --glob '!**/singleheader/**'
  --glob '!**/fuzz/**'
  --glob '!**/fuzzing/**'
  --glob '!**/autosetup/**'
  --glob '!**/jimsh0.c'
  # R11: vendored-framework excludes anchored to VENDORED LOCATIONS only, so the
  # framework's OWN shipped source is scanned when the repo IS that framework
  # (Catch2 `src/catch2/`) while embedded copies in OTHER repos are still dropped.
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/unity*'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/utest.h'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/catch.hpp'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/catch2/**'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/gtest/**'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/gmock/**'
  --glob '!**/*_test.*'
  --glob '!**/*_tests.*'
  # G7 (150-repo gauntlet): anchored test-SOURCE forms replacing the old UNANCHORED
  # `*test*.c{,c,pp,xx}` that dropped real shipped code (attestation.c/fastest.c).
  # Suffix `_test.`/`_tests.` is covered above; this adds the `test_` prefix. Kept
  # IDENTICAL to cpp_risk_scan.sh.
  --glob '!**/test_*.c'
  --glob '!**/test_*.cc'
  --glob '!**/test_*.cpp'
  --glob '!**/test_*.cxx'
  --glob '!**/*_bench*.*'
  --glob '!**/ltests.*'
  # G6 (150-repo gauntlet): test-only HEADER conventions (secp256k1 tests_impl.h /
  # tests_exhaustive_impl.h / testrand_impl.h / testutil.h). IDENTICAL to risk scan.
  --glob '!**/tests_impl.*'
  --glob '!**/tests_exhaustive*'
  --glob '!**/testrand*'
  --glob '!**/testutil.*'
  --glob '!**/tests_common.*'
  --glob '!**/unit_test.*'
  --glob '!**/wycheproof/**'
  # R3+ test/vendored/generated conventions that path-segment + suffix globs miss
  # (kept identical to cpp_risk_scan.sh so anchors line up): NASA cFE `ut-coverage/`/
  # `ut-stubs/`; CamelCase test roots (`STest/`, `FppTestProject/`, any `[A-Z]*Test/`
  # such as GTest/); the `*test_inc.h` driver-include (pcre2); generated `single_include/`
  # amalgamations (nlohmann); vendored target-libc headers under `*/win32/include/`
  # (tinycc mingw). Ordinary public `include/` is NOT excluded — only those variants.
  --glob '!**/ut-coverage/**'
  --glob '!**/ut-stubs/**'
  --glob '!**/STest/**'
  --glob '!**/*TestProject*/**'
  --glob '!**/[A-Z]*Test/**'
  --glob '!**/*test_inc.h'
  --glob '!**/single_include/**'
  --glob '!**/win32/include/**'
  # G2 (100-repo gauntlet-2 fold-back): kept IDENTICAL to cpp_risk_scan.sh so anchors
  # line up — hyphenated test/example DIRS + flat bench*/fuzzer-named FILES that the
  # path-segment + suffix globs miss (secp256k1 src/bench*.c, libwebsockets
  # minimal-examples/ + *-fuzzer). Narrow: never an implementation header/lib source.
  --glob '!**/*-test/**'
  --glob '!**/*-tests/**'
  --glob '!**/*-example/**'
  --glob '!**/*-examples/**'
  --glob '!**/test-app/**'
  --glob '!**/test-apps/**'
  --glob '!**/minimal-examples/**'
  # G7: source-named benches only (a public benchmark.h API header must survive).
  # IDENTICAL to cpp_risk_scan.sh.
  --glob '!**/bench*.{c,cc,cpp,cxx}'
  --glob '!**/*fuzzer.*'
)

# Whole-file comment/string stripper (R2): emits "<cleaned>\t<path>:<line>:<orig>"
# for every line of the files passed as args, blanking // /* */ comment spans
# (block state tracked across lines from each file's start) and "..."/'...' literal
# contents. This carries multi-line block state correctly — the only robust way,
# since the `/*` opener often lands on a line the search pattern does not match.
# A downstream rg re-applies the search pattern to the cleaned field; `cut`
# recovers the original row. Replaces the old leading-marker-only F1b filter.
STRIP_COMMENTS_AWK='
FNR == 1 { inblock = 0; linecont = 0 }
{
  print strip($0) "\t" FILENAME ":" FNR ":" $0
}
function strip(s,   out, i, c, nx, n) {
  if (linecont) {
    linecont = (substr(s, length(s), 1) == "\\")
    return ""
  }
  out = ""; n = length(s); i = 1
  while (i <= n) {
    c = substr(s, i, 1); nx = substr(s, i + 1, 1)
    if (inblock) {
      if (c == "*" && nx == "/") { inblock = 0; i += 2; continue }
      i++; continue
    }
    if (c == "/" && nx == "/") { if (substr(s, length(s), 1) == "\\") linecont = 1; break }
    if (c == "/" && nx == "*") { inblock = 1; i += 2; continue }
    if (c == "\"") {
      i++
      while (i <= n) { c = substr(s, i, 1); if (c == "\\") { i += 2; continue } if (c == "\"") { i++; break } i++ }
      out = out " "; continue
    }
    if (c == "\047") {
      i++
      while (i <= n) { c = substr(s, i, 1); if (c == "\\") { i += 2; continue } if (c == "\047") { i++; break } i++ }
      out = out " "; continue
    }
    if (c == "\t") c = " "   # keep the cleaned field tab-free so it is one
    out = out c; i++         # cut/awk field (source tab-indentation would split it)
  }
  return out
}'

# rg over SHIPPED C/C++ sources with whole-file comment/string stripping (R2/R3).
# Returns "path:line:original" rows whose CODE part matches PATTERN. Args: PATTERN REPO
rg_code() {
  local files
  files="$(rg -l --no-messages \
      --glob '*.{c,cc,cpp,cxx,h,hh,hpp,hxx}' \
      "${EXCLUDE_GLOBS[@]}" \
      "$1" "$2" 2>/dev/null | LC_ALL=C sort || true)"
  [ -n "$files" ] || return 0
  printf '%s\n' "$files" | awk 'NF' | tr '\n' '\0' \
    | xargs -0 awk "$STRIP_COMMENTS_AWK" 2>/dev/null \
    | rg -P "^[^\t]*(?:$1)" 2>/dev/null \
    | cut -f2- || true
}

# C++ signal: does the repo SHIP real C++ code? True only when an actual C++
# translation unit (.cc/.cpp/.cxx/.c++) OR C++-only header (.hpp/.hh/.hxx/.h++)
# exists in a SHIPPED (non-test/non-vendored/non-extras) dir — same exclusion set
# as the scan (R1). A lone CMAKE_CXX_STANDARD / enable_language(CXX) build variable
# or a test-only/extras .cpp must NOT count: it falsely flipped pure-C repos to C++
# and re-enabled C++-only advice. C++-only advice (std::span/string_view) is only
# emitted when this is "yes"; on pure-C repos the same surface is relabeled as a
# ptr+len ownership-contract gap (F1a / W2).
repo_has_cpp() {
  local repo="$1"
  if [ -n "$(rg --files --no-messages \
        --glob '*.{cc,cpp,cxx,c++,hpp,hh,hxx,h++}' \
        "${EXCLUDE_GLOBS[@]}" \
        "$repo" 2>/dev/null)" ]; then
    return 0
  fi
  return 1
}

# Count distinct (case-folded) matches of PATTERN under REPO across the CI/build
# globs passed as remaining args. Always prints a bare integer, never fails.
# Args: PATTERN REPO GLOB...
count_distinct() {
  local pattern="$1" repo="$2"
  shift 2
  local globs=()
  local g
  for g in "$@"; do
    globs+=(--glob "$g")
  done
  local n
  # --hidden so the dot-prefixed .github/workflows tree is searched (F3a/F6):
  # rg skips hidden dirs by default, which made CI detection blind to GH Actions.
  n="$(rg -o --hidden --no-messages -i "$pattern" "${globs[@]}" '' "$repo" 2>/dev/null \
        | tr 'A-Z' 'a-z' | LC_ALL=C sort -u | grep -c . || true)"
  [ -n "$n" ] || n=0
  printf '%s' "$n"
}

# Strip a leading "REPO/" (or "REPO" == ".") prefix so anchors are repo-relative.
# Args: REPO  (reads file:line tokens on stdin, rewrites the path component)
strip_repo_prefix() {
  local repo="$1"
  local prefix="$repo/"
  awk -v prefix="$prefix" '
    {
      # token is path:line:rest (grep output) OR a bare path (rg -l --files).
      n = index($0, ":")
      if (n == 0) {
        # bare path: strip ./ and the repo prefix, then print.
        path = $0
        sub(/^\.\//, "", path)
        if (substr(path, 1, length(prefix)) == prefix) {
          path = substr(path, length(prefix) + 1)
        }
        print path
        next
      }
      path = substr($0, 1, n - 1)
      rest = substr($0, n)
      # normalize ./ and the repo prefix
      sub(/^\.\//, "", path)
      if (substr(path, 1, length(prefix)) == prefix) {
        path = substr(path, length(prefix) + 1)
      }
      print path rest
    }'
}

# ---------------------------------------------------------------------------
# Lane producers. Each appends "lane\tgap\tanchor" lines to the backlog buffer.
# A line is only appended when a real anchor exists.
# ---------------------------------------------------------------------------

# hardening: build-file evidence gaps (anchor = inventory/build key) ----------
emit_hardening_build() {
  local repo="$1"
  local inv="$2"   # captured cpp_inventory.sh output
  local buildfiles
  # The set of build/config files that could carry hardening flags.
  buildfiles="$(rg -l --no-messages \
      --glob 'CMakeLists.txt' --glob '*.cmake' --glob 'CMakePresets.json' \
      --glob 'meson.build' --glob 'Makefile' --glob '*.mk' \
      --glob '!**/.git/**' --glob '!**/build/**' --glob '!**/_deps/**' \
      '' "$repo" 2>/dev/null || true)"
  [ -n "$buildfiles" ] || return 0

  # Each missing hardening signal is anchored to the build-config inventory key,
  # so the row is grounded even though the evidence is an absence.
  if ! rg -q --no-messages '\-fsanitize|sanitizers|CMakePresets\.sanitizers' "$repo" 2>/dev/null; then
    printf 'hardening\tno sanitizer preset (-fsanitize) found in build files\tinventory:build.config\n'
  fi
  if ! rg -q --no-messages '_FORTIFY_SOURCE' "$repo" 2>/dev/null; then
    printf 'hardening\tno -D_FORTIFY_SOURCE in build files\tinventory:build.config\n'
  fi
  if ! rg -q --no-messages '\-fstack-protector|stack_protector' "$repo" 2>/dev/null; then
    printf 'hardening\tno stack-protector flag in build files\tinventory:build.config\n'
  fi
  if ! rg -q --no-messages '\-fcf-protection|\-fsanitize=cfi|cf-protection' "$repo" 2>/dev/null; then
    printf 'hardening\tno CFI / control-flow-protection evidence in build files\tinventory:build.config\n'
  fi
  return 0
}

# hardening: unsafe API call sites (anchor = file:line) ----------------------
emit_hardening_calls() {
  local repo="$1"
  local hits
  hits="$(rg_code '\b(strcpy|strcat|sprintf|gets)\s*\(' "$repo" | strip_repo_prefix "$repo")"
  if [ -n "$hits" ]; then
    while IFS= read -r line; do
      [ -n "$line" ] || continue
      local anchor api
      anchor="$(printf '%s' "$line" | cut -d: -f1-2)"
      # `|| true`: under `set -euo pipefail` a `grep` that matches nothing exits 1
      # and pipefail propagates it through `| head`, aborting the substitution and
      # the whole script (N-cmphang abort class). A no-match is benign here.
      api="$(printf '%s' "$line" | grep -oE '\b(strcpy|strcat|sprintf|gets)\b' | head -n1 || true)"
      printf 'hardening\tunsafe string/format API %s (bounded-copy migration candidate)\t%s\n' "$api" "$anchor"
    done <<EOF
$hits
EOF
  fi
  # unchecked malloc with a multiply in the size argument (overflow risk)
  hits="$(rg_code '\b(malloc|calloc|realloc)\s*\([^)]*\*' "$repo" | strip_repo_prefix "$repo")"
  if [ -n "$hits" ]; then
    while IFS= read -r line; do
      [ -n "$line" ] || continue
      local anchor
      anchor="$(printf '%s' "$line" | cut -d: -f1-2)"
      printf 'hardening\tmalloc/calloc with a multiply in size arg (overflow guard candidate)\t%s\n' "$anchor"
    done <<EOF
$hits
EOF
  fi
  return 0
}

# api-ergonomics: pointer+length signatures, owning raw alloc in headers ------
#
# R1-mixed: the std::span/string_view recommendation is a C++-only construct, so the
# span-vs-C-ownership decision is made PER FILE by extension, not repo-wide. The old
# code keyed on the repo-level `repo_has_cpp` signal: on a MIXED C/C++ repo (zephyr:
# 8849 .c + 50 .cpp) that returned yes, so EVERY ptr+len hit — including the ~9400 in
# pure-C `arch/*/*.c` — got a "no std::span/string_view" suggestion that is N/A in C.
# Now a hit in a C++ TU/header (.cc/.cpp/.cxx/.hpp/.hh/.hxx/.h++) gets the span advice;
# a hit in a `.c`/`.h` file gets the W2 C ownership-contract relabel (span is N/A in C
# regardless of whether the repo uses span elsewhere). The same per-file rule gates
# the owning-raw `new` arm: `= new …` only flags in a C++ file (in a C file `new` is a
# legal identifier), while `= malloc/calloc(…)` flags in any header.
is_cpp_path() {
  case "$1" in
    *.cc|*.cpp|*.cxx|*.c++|*.hpp|*.hh|*.hxx|*.h++) return 0 ;;
    *) return 1 ;;
  esac
}

emit_api_ergonomics() {
  local repo="$1"
  local hits has_span
  # Does the repo already use span/string_view anywhere? If so, suppress the span
  # suggestion for C++ files (it has the vocabulary; absence elsewhere is noise).
  # This NEVER suppresses the C-file ownership-contract relabel, which is span-
  # independent (a C file cannot use std::span no matter what the C++ TUs do).
  has_span=no
  if rg -q --no-messages 'std::span|std::string_view|gsl::span|absl::Span' "$repo" 2>/dev/null; then
    has_span=yes
  fi
  # pointer+length parameter pair in a signature: `T *name, size_t len`. Scanned over
  # the whole shipped C/C++ surface; the C++-vs-C label is chosen per hit by extension.
  hits="$(rg_code '\*[A-Za-z_][A-Za-z0-9_]*\s*,\s*(size_t|std::size_t|unsigned|int|uint[0-9]+_t)\s+[A-Za-z_]*(len|size|count|n)[A-Za-z0-9_]*' "$repo" | strip_repo_prefix "$repo")"
  if [ -n "$hits" ]; then
    while IFS= read -r line; do
      [ -n "$line" ] || continue
      local anchor path
      anchor="$(printf '%s' "$line" | cut -d: -f1-2)"
      path="$(printf '%s' "$anchor" | cut -d: -f1)"
      if is_cpp_path "$path"; then
        # C++ file: recommend std::span/string_view — unless the repo already uses it.
        [ "$has_span" = yes ] && continue
        printf 'api-ergonomics\tpointer+length parameter pair with no std::span/string_view (misuse-prone surface)\t%s\n' "$anchor"
      else
        # C file (.c/.h): ptr+len IS the idiom and span is N/A — relabel as the W2
        # ownership-contract documentation gap (per-file, even on a mixed repo).
        printf 'api-ergonomics\tpointer+length parameter pair (C: document the ptr+len ownership/bounds contract)\t%s\n' "$anchor"
      fi
    done <<EOF
$hits
EOF
  fi
  # owning raw new/malloc inside a header (ownership crosses the boundary). We scan
  # headers with the `new`-INCLUSIVE pattern but only KEEP a `new` hit when it lands
  # in a C++ header (.hpp/.hh/.hxx/.h++); a `.h` C header keeps only malloc/calloc, so
  # a C var named `new` (`x->new = …`) is never read as a C++ new-expression (R1-mixed).
  local owner_pat
  owner_pat='(=|return)\s*(::)?(new\b|malloc\s*\(|calloc\s*\()'
  local hdr_files
  hdr_files="$(rg -l --no-messages \
      --glob '*.{h,hh,hpp,hxx,h++}' \
      "${EXCLUDE_GLOBS[@]}" \
      "$owner_pat" "$repo" 2>/dev/null | LC_ALL=C sort || true)"
  if [ -n "$hdr_files" ]; then
    hits="$(printf '%s\n' "$hdr_files" | awk 'NF' | tr '\n' '\0' \
        | xargs -0 awk "$STRIP_COMMENTS_AWK" 2>/dev/null \
        | rg -P "^[^\t]*(?:$owner_pat)" 2>/dev/null \
        | cut -f2- | strip_repo_prefix "$repo" || true)"
  else
    hits=""
  fi
  if [ -n "$hits" ]; then
    while IFS= read -r line; do
      [ -n "$line" ] || continue
      local anchor path
      anchor="$(printf '%s' "$line" | cut -d: -f1-2)"
      path="$(printf '%s' "$anchor" | cut -d: -f1)"
      # A `new`-only hit in a C header is a C identifier, not a C++ new-expression:
      # keep it only when the line carries malloc/calloc OR the file is a C++ header.
      if ! is_cpp_path "$path"; then
        if ! printf '%s' "$line" | grep -qE '(=|return)[[:space:]]*(malloc|calloc)[[:space:]]*\('; then
          continue
        fi
      fi
      printf 'api-ergonomics\towning raw new/malloc in a header (ownership crosses boundary; owner-annotated/RAII candidate)\t%s\n' "$anchor"
    done <<EOF
$hits
EOF
  fi
  return 0
}

# portability: single-toolchain CI, load-bearing width/endian assumptions -----
emit_portability() {
  local repo="$1"
  local ci_files compilers archs stds hits has_matrix
  # CI matrix files (anchor = the CI file path when present, else inventory key).
  # --hidden: .github/workflows is a dot-dir that rg skips by default, which made
  # the whole lane blind to GitHub Actions and over-report "no CI matrix" (F3a/F6).
  ci_files="$(rg -l --hidden --no-messages \
      --glob '**/.github/workflows/*.yml' --glob '**/.github/workflows/*.yaml' \
      --glob '**/.gitlab-ci.yml' --glob '**/azure-pipelines.yml' \
      --glob '**/.cirrus.yml' --glob '**/.travis.yml' --glob '**/appveyor.yml' \
      '' "$repo" 2>/dev/null | strip_repo_prefix "$repo" || true)"
  local ci_anchor
  if [ -n "$ci_files" ]; then
    ci_anchor="$(printf '%s\n' "$ci_files" | LC_ALL=C sort | head -n1)"
  else
    ci_anchor="inventory:ci.matrix"
  fi

  local ci_globs=(
    '**/.github/workflows/*.yml' '**/.github/workflows/*.yaml'
    '**/.gitlab-ci.yml' '**/azure-pipelines.yml'
    '**/.cirrus.yml' '**/.travis.yml' '**/appveyor.yml'
  )
  if [ -n "$ci_files" ]; then
    # Does any CI file declare an explicit build/test matrix (strategy.matrix,
    # a YAML matrix: key, or a multi-value compiler/arch list)?  If so we never
    # claim CI is missing; we report what the matrix covers instead (F3a).
    has_matrix=no
    if rg -q --hidden --no-messages -i 'strategy:|matrix:|include:|fail-fast:' \
        --glob '**/.github/workflows/*.yml' --glob '**/.github/workflows/*.yaml' \
        '' "$repo" 2>/dev/null; then
      has_matrix=yes
    fi
    # Count distinct compilers / arches mentioned across CI files.
    compilers="$(count_distinct '\b(gcc|g\+\+|clang|clang\+\+|cl\.exe|msvc|mingw)\b' "$repo" "${ci_globs[@]}")"
    archs="$(count_distinct '\b(x86_64|amd64|aarch64|arm64|armv7|armv8|thumb|mips|powerpc|ppc64|i686|riscv64|s390x|win32|win64)\b' "$repo" "${ci_globs[@]}")"
    if [ "$has_matrix" = yes ]; then
      printf 'portability\tCI matrix present (covers %s compiler(s), %s arch(es)); verify it spans intended targets\t%s\n' \
        "${compilers:-0}" "${archs:-0}" "$ci_anchor"
    else
      if [ "${compilers:-0}" -le 1 ]; then
        printf 'portability\tonly one compiler exercised in CI (add a second toolchain)\t%s\n' "$ci_anchor"
      fi
      if [ "${archs:-0}" -le 1 ]; then
        printf 'portability\tonly one architecture exercised in CI (add a second arch)\t%s\n' "$ci_anchor"
      fi
    fi
  else
    printf 'portability\tno CI matrix detected; toolchain/arch/std coverage is unproven\t%s\n' "$ci_anchor"
  fi

  # Standard versions exercised anywhere in build/CI config.
  stds="$(count_distinct '(c\+\+(98|03|11|14|17|20|23)|gnu\+\+[0-9]+|std=c[0-9]+|c(89|99|11|17|23))' "$repo" \
      '**/CMakeLists.txt' '**/*.cmake' '**/CMakePresets.json' '**/meson.build' '**/Makefile' '**/*.mk' \
      '**/.github/workflows/*.yml' '**/.github/workflows/*.yaml')"
  if [ "${stds:-0}" -le 1 ]; then
    printf 'portability\tat most one language standard exercised in build/CI\t%s\n' "$ci_anchor"
  fi

  # Load-bearing width / time_t / endian / packing assumptions (anchor = file:line).
  hits="$(rg_code '\b(htons|htonl|ntohs|ntohl|__BYTE_ORDER__|BYTE_ORDER|__attribute__\s*\(\s*\(\s*packed|#pragma pack)\b' "$repo" | strip_repo_prefix "$repo")"
  if [ -n "$hits" ]; then
    while IFS= read -r line; do
      [ -n "$line" ] || continue
      local anchor
      anchor="$(printf '%s' "$line" | cut -d: -f1-2)"
      printf 'portability\tendian/packing assumption (load-bearing, needs a static_assert or portable accessor)\t%s\n' "$anchor"
    done <<EOF
$hits
EOF
  fi
  hits="$(rg_code '\b(time_t)\b' "$repo" | strip_repo_prefix "$repo")"
  if [ -n "$hits" ]; then
    while IFS= read -r line; do
      [ -n "$line" ] || continue
      local anchor
      anchor="$(printf '%s' "$line" | cut -d: -f1-2)"
      printf 'portability\ttime_t width assumption (verify against 32-bit / Y2038 targets)\t%s\n' "$anchor"
    done <<EOF
$hits
EOF
  fi
  return 0
}

# test-fuzz-coverage: parse/decode entry points with no fuzz harness ----------
emit_test_fuzz() {
  local repo="$1"
  local has_fuzz fuzz_refs fuzz_files hits
  # (a, R6) OSS-Fuzz / CIFuzz / ClusterFuzzLite INTEGRATION. A project wired into
  # continuous fuzzing — a `.github/workflows/cifuzz.yml`, a `.clusterfuzzlite/` dir,
  # or a workflow that references the `google/oss-fuzz`/`google/clusterfuzzlite` action
  # or an `oss-fuzz-project-name:` key — ships fuzzing as infrastructure, not as a few
  # in-tree harnesses we can resolve file-by-file. For these the lane must NOT emit
  # "no fuzz harness" per entry point (nlohmann_json had 39, all false: it runs CIFuzz
  # on every PR). We detect the integration and SUPPRESS per-entry flagging, emitting a
  # single informational row instead. --hidden so a dot-prefixed CI layout is seen.
  local fuzz_integration=no
  if [ -f "$repo/.github/workflows/cifuzz.yml" ] \
     || [ -d "$repo/.clusterfuzzlite" ] \
     || [ -n "$(rg -l --hidden --no-messages \
                  -e 'oss-fuzz-project-name' -e 'google/oss-fuzz' -e 'google/clusterfuzzlite' \
                  "$repo/.github" 2>/dev/null || true)" ]; then
    fuzz_integration=yes
  fi

  # Does any fuzz harness exist at all?  A fuzz*/ dir, any *fuzz* source file, or
  # any LLVMFuzzerTestOneInput definition counts (F3b). --hidden so a dot-prefixed
  # OSS-Fuzz layout is still seen.
  has_fuzz=no
  # Files that ARE fuzz harnesses, found by path (fuzz*/ dir or *fuzz* name) or by
  # an LLVMFuzzerTestOneInput entry point. --hidden so a dot-prefixed OSS-Fuzz
  # layout is still seen. We must never flag one of these as an "uncovered"
  # parser, and a parser referenced by one is COVERED (F3b).
  local fuzz_paths_abs
  fuzz_paths_abs="$(rg --files --hidden --no-messages \
      --glob '**/fuzz/**' --glob '**/fuzzing/**' --glob '**/*fuzz*' \
      --glob '!**/.git/**' \
      "$repo" 2>/dev/null || true)"
  local refs_abs
  refs_abs="$(rg -l --hidden --no-messages 'LLVMFuzzerTestOneInput' "$repo" 2>/dev/null || true)"
  fuzz_files="$(printf '%s\n' "$fuzz_paths_abs" | strip_repo_prefix "$repo" | awk 'NF')"
  fuzz_refs="$(printf '%s\n' "$refs_abs" | strip_repo_prefix "$repo" | awk 'NF')"
  if [ -n "$fuzz_files" ] || [ -n "$fuzz_refs" ]; then
    has_fuzz=yes
  fi

  # (a) When the project integrates continuous fuzzing, do NOT walk per-entry: emit
  # one informational row and return (so the backlog records the fact without 39-402
  # false "uncovered" rows). This is the single most impactful R6 false-positive fix.
  if [ "$fuzz_integration" = yes ]; then
    printf 'test-fuzz-coverage\tproject integrates continuous fuzzing (OSS-Fuzz/CIFuzz); per-entry coverage not flagged\t.github/workflows\n'
    return 0
  fi
  # Repo-relative harness file list (so we never flag a harness as uncovered).
  local harness_index
  harness_index="$(printf '%s\n%s\n' "$fuzz_files" "$fuzz_refs" | awk 'NF' | LC_ALL=C sort -u)"
  # Harness corpus: the concatenated text of every harness file, in a bounded
  # TEMP FILE (not a shell var). A parser entry is covered when its function name
  # OR its source file's basename appears here (the harness #includes the file or
  # calls the function). Built as a file so the per-entry coverage check greps a
  # file instead of re-materializing a giant shell string per entry (that was an
  # O(entries x corpus) blowup), and so a BINARY harness file (a checked-in seed
  # corpus or .a) never gets cat'd into a shell var (which floods NUL warnings and
  # bloats memory -> the s2n-tls timeout the 100-repo gauntlet found). We skip
  # binary files (grep -I), strip stray NULs, cap per-file and total size, and cap
  # the file count, so the corpus is bounded regardless of the repo.
  local corpus_file=""
  if [ "$has_fuzz" = yes ]; then
    corpus_file="$(mktemp 2>/dev/null)" || corpus_file=""
    if [ -n "$corpus_file" ]; then
      local absf hc_n=0
      while IFS= read -r absf; do
        [ -n "$absf" ] || continue
        hc_n=$((hc_n + 1)); [ "$hc_n" -gt 200 ] && break          # cap harness-file count
        grep -Iq . "$absf" 2>/dev/null || continue                # text only; skip binary
        LC_ALL=C tr -d '\0' < "$absf" 2>/dev/null | head -c 131072 >> "$corpus_file"
        printf '\n' >> "$corpus_file"
        [ "$(wc -c <"$corpus_file" 2>/dev/null || echo 0)" -gt 4194304 ] && break  # cap total 4MB
      done <<EOF
$(printf '%s\n%s\n' "$fuzz_paths_abs" "$refs_abs" | awk 'NF' | LC_ALL=C sort -u)
EOF
    fi
  fi

  # parser/decoder entry points: name matches *parse*/*decode*, or signature
  # takes a raw byte buffer + length (the canonical fuzz entry shape). We anchor
  # on `const uint8_t*`/`const unsigned char*` + size_t and deliberately exclude
  # `const char*`, which is overwhelmingly a C string, not an untrusted byte run.
  hits="$(rg_code '\b[A-Za-z_][A-Za-z0-9_]*(parse|decode|Parse|Decode)[A-Za-z0-9_]*\s*\(' "$repo" | strip_repo_prefix "$repo")"
  local sig_hits
  sig_hits="$(rg_code 'const\s+(uint8_t|unsigned char)\s*\*[A-Za-z0-9_ ]*,\s*(size_t|std::size_t)' "$repo" | strip_repo_prefix "$repo")"
  if [ -n "$sig_hits" ]; then
    if [ -n "$hits" ]; then
      hits="$hits
$sig_hits"
    else
      hits="$sig_hits"
    fi
  fi

  # (c, R6) Keep only PUBLIC ENTRY-POINT DEFINITIONS. The raw regex matched any line
  # containing a `*parse*(`/`*decode*(` token — including (1) internal-linkage
  # definitions (`static`, `DUK_LOCAL`, `DUK_INTERNAL`, a private `duk__`/`_`-prefixed
  # name), (2) prototypes/declarations and call statements (line ends with `;`), and
  # (3) call-sites inside an expression (`x = foo_decode(...)`, `if (json_parse(...))`).
  # None of those is a fuzzable PUBLIC entry point. The bulk of duktape's 402 false
  # rows were exactly these (86 DUK_LOCAL + 41 DUK_INTERNAL + ~253 call/proto lines).
  # This awk keeps a row only when the matched name is a function DEFINITION: external
  # linkage, not a private prefix, not a `;`-terminated decl/call, not a control opener,
  # not preceded by an operator/`.`/`->`/`(` (a call), and the line opens a body (`{`),
  # continues a signature (`,`), or ends the parameter list (`)`) — the definition shapes.
  hits="$(printf '%s\n' "$hits" | awk -F: '
    # The entry-token regex: a function-CALL identifier whose name contains parse/decode
    # (as a prefix, middle, or suffix: parse_packet, json_parse, duk_json_decode), OR the
    # canonical fuzz-entry signature (a const byte-buffer + size_t). Both are entry shapes.
    function has_token(c) {
      return (c ~ /(^|[^A-Za-z0-9_])[A-Za-z0-9_]*(parse|decode|Parse|Decode)[A-Za-z0-9_]*[ \t]*\(/) \
          || (c ~ /const[ \t]+(uint8_t|unsigned char)[ \t]*\*[A-Za-z0-9_ ]*,[ \t]*(size_t|std::size_t)/)
    }
    NF >= 2 {
      path=$1; lineno=$2;
      code=$0; sub(/^[^:]*:[^:]*:/, "", code); sub(/^[ \t]+/, "", code)
      if (!has_token(code)) next
      # Internal linkage / forward declaration macros are not public fuzz entry points.
      if (code ~ /^static[ \t]/) next
      if (code ~ /(^|[^A-Za-z0-9_])DUK_LOCAL([^A-Za-z0-9_]|$)/) next
      # DUK_INTERNAL and DUK_INTERNAL_DECL (and any *_DECL prototype macro) are internal/
      # forward declarations, not public definitions.
      if (code ~ /(^|[^A-Za-z0-9_])DUK_INTERNAL/) next
      if (code ~ /(^|[^A-Za-z0-9_])[A-Z][A-Z0-9_]*_DECL[ \t]/) next
      # private-prefixed entry name (duk__foo / _foo / __foo) => internal, skip
      if (code ~ /(^|[^A-Za-z0-9_])(duk__|__|_)[A-Za-z0-9_]*(parse|decode|Parse|Decode)[A-Za-z0-9_]*[ \t]*\(/) next
      # prototype/declaration or statement/call (ends with ;) => not a definition
      if (code ~ /;[ \t]*$/) next
      # control-keyword opener => not a definition
      if (code ~ /^(if|for|while|switch|return|else|do)([ \t]|\()/) next
      # call/expression context: if a parse/decode-token call appears, inspect the char
      # immediately before it — a definition has a TYPE/qualifier word char (or `*`)
      # before the name; a call/expression has an operator/punctuator or `->`.
      m = match(code, /[A-Za-z_][A-Za-z0-9_]*(parse|decode|Parse|Decode)[A-Za-z0-9_]*[ \t]*\(/)
      if (m > 1) {
        before = substr(code, 1, m - 1)
        sub(/[ \t]+$/, "", before)
        last = substr(before, length(before), 1)
        if (last != "" && last !~ /[A-Za-z0-9_*]/) next
        if (before ~ /->$/) next
      }
      # a definition line opens a body, continues a signature, or ends the param list
      if (code !~ /[{,][ \t]*$/ && code !~ /\)[ \t]*$/) next
      print path ":" lineno ":" code
    }')"

  [ -n "$hits" ] || return 0

  while IFS= read -r line; do
    [ -n "$line" ] || continue
    local anchor fname base content func
    anchor="$(printf '%s' "$line" | cut -d: -f1-2)"
    fname="$(printf '%s' "$anchor" | cut -d: -f1)"
    base="$(basename "$fname")"
    # Never flag the fuzz harness file itself as an uncovered entry point (F3b).
    if printf '%s\n' "$harness_index" | grep -qxF "$fname" 2>/dev/null; then
      continue
    fi
    if [ "$has_fuzz" = yes ] && [ -n "$corpus_file" ] && [ -s "$corpus_file" ]; then
      # Covered if a harness #includes this file (basename appears in corpus)...
      # grep the bounded corpus FILE (not a per-entry printf of a giant shell var).
      if grep -qF "$base" "$corpus_file" 2>/dev/null; then
        continue
      fi
      # ...or a harness calls the entry-point function by name. Extract the
      # identifier immediately before the FIRST "(" on the line (the declared/
      # defined function), then look for it referenced in the harness corpus.
      content="$(printf '%s' "$line" | cut -d: -f3-)"
      # `|| true`: either `grep` can match nothing (the line's only `name(` is a
      # control keyword filtered by the `grep -v`), exiting 1; under pipefail that
      # status survives `| head | sed` and aborts the whole script (N-cmphang class).
      func="$(printf '%s' "$content" \
        | grep -oE '[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(' \
        | grep -vwE '(if|for|while|switch|return|sizeof|defined)[[:space:]]*\(' \
        | head -n1 | sed -E 's/[[:space:]]*\($//' || true)"
      if [ -n "$func" ] && grep -qE "(^|[^A-Za-z0-9_])${func}([^A-Za-z0-9_]|$)" "$corpus_file" 2>/dev/null; then
        continue
      fi
    fi
    printf 'test-fuzz-coverage\tparser/decoder entry point with no fuzz harness referencing it\t%s\n' "$anchor"
  done <<EOF
$hits
EOF
  [ -n "$corpus_file" ] && rm -f "$corpus_file" 2>/dev/null
  return 0
}

# ---------------------------------------------------------------------------
# Backlog assembly: gather rows, dedupe, sort deterministically.
# ---------------------------------------------------------------------------
build_backlog() {
  local repo="$1"
  local inv=""
  # cpp_inventory.sh is read-only; capture it (a couple of lanes anchor to it).
  if [ -x "$SELF_DIR/cpp_inventory.sh" ] || [ -f "$SELF_DIR/cpp_inventory.sh" ]; then
    inv="$(bash "$SELF_DIR/cpp_inventory.sh" "$repo" 2>/dev/null || true)"
  fi
  # NOTE (G2 fold-back): cpp_backlog reproduces the risk anchors it needs via its own
  # rg_code lanes below; it does NOT consume cpp_risk_scan.sh's output. A previous
  # version still RAN cpp_risk_scan.sh here and discarded the result (>/dev/null) "for
  # parity" — pure dead weight that added risk_scan's full runtime to every backlog
  # invocation (52s on the mongoose amalgamation, seconds on every repo) and caused
  # cpp_backlog to time out on large repos (nuttx, mongoose) in the 100-repo gauntlet.
  # Removed: the output was unused, so deleting the call changes nothing but the speed.

  # G2 fold-back (large-repo guard, same pattern as cpp_comprehension_map --exact):
  # the per-hit/per-entry lanes (hardening-calls, api-ergonomics, test-fuzz) loop in
  # shell over every match, so on a huge tree (nuttx: 16915 source files) they blow
  # past any sane budget and the tool effectively hangs. Above BACKLOG_FILE_CAP run
  # only the cheap repo-level lanes (hardening-build, portability) + a scope note;
  # the per-hit lanes are honest to skip there ("scope to a subdirectory"), and they
  # also flood low-signal rows at that scale anyway. Small/normal repos are untouched.
  local srcn
  srcn="$(rg --files --no-messages --glob '*.{c,cc,cpp,cxx,h,hh,hpp,hxx}' \
      --glob '!**/.git/**' "${EXCLUDE_GLOBS[@]}" "$repo" 2>/dev/null | wc -l | tr -d ' ')"
  {
    emit_hardening_build "$repo" "$inv"
    emit_portability "$repo"
    if [ "${srcn:-0}" -le "${BACKLOG_FILE_CAP:-8000}" ]; then
      emit_hardening_calls "$repo"
      emit_api_ergonomics "$repo"
      emit_test_fuzz "$repo"
    else
      printf 'backlog-scope\trepo too large (%s source files > %s): per-hit lanes (unsafe-call / api-ergonomics / test-fuzz-coverage) skipped to stay in budget — run cpp_backlog.sh on a touched subdirectory for those\t(repo root)\n' \
        "$srcn" "${BACKLOG_FILE_CAP:-8000}"
    fi
  } | awk 'NF' | LC_ALL=C sort -u
}

emit_text() {
  # tab-separated lane/gap/anchor -> "lane | gap | anchor"
  awk -F'\t' 'NF>=3 { printf "%s | %s | %s\n", $1, $2, $3 }'
}

emit_json() {
  # tab-separated lane/gap/anchor -> JSON array of {lane,gap,anchor}
  awk -F'\t' '
    function esc(s,   r) {
      r = s
      gsub(/\\/, "\\\\", r)
      gsub(/"/, "\\\"", r)
      return r
    }
    BEGIN { printf "[" ; first = 1 }
    NF>=3 {
      if (!first) printf ","
      first = 0
      printf "\n  {\"lane\": \"%s\", \"gap\": \"%s\", \"anchor\": \"%s\"}", esc($1), esc($2), esc($3)
    }
    END { if (!first) printf "\n"; printf "]\n" }'
}

run_backlog() {
  local repo="$1"
  local json="$2"
  if [ ! -d "$repo" ]; then
    printf 'error: not a directory: %s\n' "$repo" >&2
    exit 2
  fi
  if ! rg_available; then
    printf 'error: rg (ripgrep) is required for cpp_backlog.sh\n' >&2
    exit 3
  fi
  local rows
  rows="$(build_backlog "$repo")"
  if [ "$json" = yes ]; then
    printf '%s\n' "$rows" | emit_json
  else
    printf '%s\n' "$rows" | emit_text
  fi
}

# ---------------------------------------------------------------------------
# Self-test: build a tiny fake repo, prove rows appear / disappear, prove
# two consecutive runs byte-match. Cleans up via trap.
# ---------------------------------------------------------------------------
self_test() {
  if ! rg_available; then
    printf 'cpp_backlog self-test: FAIL (rg not available)\n'
    exit 1
  fi
  local tmp
  tmp="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp'" EXIT

  mkdir -p "$tmp/src"
  # Build file with no hardening flags -> hardening build rows. R1 TRAP: it also
  # declares CMAKE_CXX_STANDARD + enable_language(CXX) for an auxiliary target,
  # which previously flipped this pure-C repo to "C++" and leaked std::span advice.
  # With zero shipped C++ TUs the repo MUST stay C (asserted below via the C
  # ownership-contract relabel + the no-span assertion).
  cat >"$tmp/CMakeLists.txt" <<'CM'
cmake_minimum_required(VERSION 3.16)
project(fake C)
set(CMAKE_CXX_STANDARD 17)
enable_language(CXX)
add_executable(fake src/main.c)
CM
  # Source with an injected strcpy and a parse_* entry point, no fuzz harness.
  # Also seeds comment-only prose that MUST NOT match (F1b/F1c): a doc comment
  # mentioning strcpy/sprintf/new/delete, and a ptr+len signature (F1a-C). The
  # R2 TRAP is a MULTI-LINE /* ... */ block whose CONTINUATION line mentions
  # `malloc()` with no leading marker (the leveldb c.h class of leak) — it must
  # NOT yield a hardening row; only the real strcpy call may.
  cat >"$tmp/src/main.c" <<'SRC'
#include <string.h>
#include <stdlib.h>
/* This routine used to call sprintf and strcpy; we now use snprintf. */
// delete the old buffer and allocate a new one (prose, not code)
/* REQUIRES: ptr was
   malloc()-ed by the caller and is freed here */
void copy_it(char *dst, const char *src) {
    strcpy(dst, src);
}
int read_bytes(char *buf, size_t len) {
    return (int)(buf[0] + len);
}
int parse_packet(const unsigned char *buf, size_t len) {
    return (int)(buf[0] + len);
}
SRC

  # A test-tree file with a real strcpy CALL that MUST be excluded (F7).
  mkdir -p "$tmp/tests"
  cat >"$tmp/tests/test_main.c" <<'SRC'
#include <string.h>
void t(char *d, const char *s) { strcpy(d, s); }
SRC

  # R3 TRAP: a SUFFIX-named test co-located with sources (Google style) carrying
  # a real strcpy CALL. No dir-glob catches it; the *_test.* filename filter must.
  cat >"$tmp/src/parser_test.c" <<'SRC'
#include <string.h>
void tp(char *d, const char *s) { strcpy(d, s); }
SRC

  # R3+ TRAP: real strcpy CALLS inside the NASA `ut-coverage/`/`ut-stubs/` test
  # convention and a vendored `win32/include/` target-libc header. The new R3+
  # exclusion globs must drop all three (the shipped `src/main.c` strcpy survives).
  mkdir -p "$tmp/modules/ut-coverage" "$tmp/modules/ut-stubs" "$tmp/win32/include"
  cat >"$tmp/modules/ut-coverage/cov.c" <<'SRC'
#include <string.h>
void cov(char *d, const char *s) { strcpy(d, s); }
SRC
  cat >"$tmp/modules/ut-stubs/stub.c" <<'SRC'
#include <string.h>
void stub(char *d, const char *s) { strcpy(d, s); }
SRC
  cat >"$tmp/win32/include/string.h" <<'SRC'
#include <string.h>
static void w32(char *d, const char *s) { strcpy(d, s); }
SRC

  local out1 out2
  out1="$(run_backlog "$tmp" no)"

  # Assertion 1: a hardening strcpy row appears, anchored to main.c.
  if ! printf '%s\n' "$out1" | grep -q 'hardening | unsafe string/format API strcpy'; then
    printf 'cpp_backlog self-test: FAIL (expected strcpy hardening row absent)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  if ! printf '%s\n' "$out1" | grep -qE 'src/main\.c:[0-9]+'; then
    printf 'cpp_backlog self-test: FAIL (strcpy row carries no repo-relative anchor)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  # Assertion 2: a test-fuzz-coverage row appears for parse_packet.
  if ! printf '%s\n' "$out1" | grep -q 'test-fuzz-coverage | parser/decoder entry point with no fuzz harness'; then
    printf 'cpp_backlog self-test: FAIL (expected parser/no-fuzz row absent)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  # Assertion F1b/F1c: prose in comments (sprintf/strcpy/new/delete words) must
  # NOT yield a hardening row. The only unsafe-string row may be the real call.
  if printf '%s\n' "$out1" | grep -qE 'main\.c:4'; then
    printf 'cpp_backlog self-test: FAIL (matched a comment-only line, F1b)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  # Assertion F7: a strcpy CALL inside tests/ must be excluded.
  if printf '%s\n' "$out1" | grep -qE 'tests/test_main\.c'; then
    printf 'cpp_backlog self-test: FAIL (tests/ tree not excluded, F7)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  # Assertion R3: a SUFFIX-named *_test.c co-located in src/ must be excluded.
  if printf '%s\n' "$out1" | grep -qE 'src/parser_test\.c'; then
    printf 'cpp_backlog self-test: FAIL (suffix-named *_test.c not excluded, R3)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  # Assertion R3+: NASA `ut-coverage/`/`ut-stubs/` and vendored `win32/include/`
  # hits must be excluded, while the real shipped `src/main.c` strcpy survives.
  if printf '%s\n' "$out1" | grep -qE 'ut-coverage/|ut-stubs/|win32/include/'; then
    printf 'cpp_backlog self-test: FAIL (R3+: ut-coverage/ut-stubs/win32-include path not excluded)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  if ! printf '%s\n' "$out1" | grep -qE 'src/main\.c:[0-9]+'; then
    printf 'cpp_backlog self-test: FAIL (R3+ over-correction: real shipped src/main.c hit dropped)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  # Assertion R2: the multi-line block-comment CONTINUATION line that mentions
  # malloc() (no leading marker) must NOT produce a hardening malloc row. The
  # malloc-with-multiply row anchors only to real code; the doc line at main.c:6
  # must not appear anywhere.
  if printf '%s\n' "$out1" | grep -qE 'main\.c:6'; then
    printf 'cpp_backlog self-test: FAIL (block-comment continuation leaked a malloc row, R2)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  # Assertion F1a (C): on a pure-C repo, ptr+len advice is relabeled as an
  # ownership-contract gap, NOT a std::span/string_view suggestion.
  if ! printf '%s\n' "$out1" | grep -q 'api-ergonomics | pointer+length parameter pair (C: document the ptr+len ownership/bounds contract)'; then
    printf 'cpp_backlog self-test: FAIL (C ptr+len ownership-contract row absent, F1a)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  if printf '%s\n' "$out1" | grep -q 'no std::span/string_view'; then
    printf 'cpp_backlog self-test: FAIL (std::span advice emitted on a pure-C repo, F1a)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  # Assertion 3 (no absolute paths leak): output must not contain the temp dir.
  if printf '%s\n' "$out1" | grep -qF "$tmp"; then
    printf 'cpp_backlog self-test: FAIL (absolute path leaked into output)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out1"
    exit 1
  fi
  # Assertion 4: reproducibility - two consecutive runs byte-match.
  out2="$(run_backlog "$tmp" no)"
  if [ "$out1" != "$out2" ]; then
    printf 'cpp_backlog self-test: FAIL (two runs did not byte-match)\n'
    diff <(printf '%s\n' "$out1") <(printf '%s\n' "$out2") || true
    exit 1
  fi

  # Now remove the injected gaps and assert the rows disappear.
  cat >"$tmp/src/main.c" <<'SRC'
#include <string.h>
#include <stdlib.h>
void copy_it(char *dst, const char *src, size_t cap) {
    snprintf(dst, cap, "%s", src);
}
int compute(int a, int b) {
    return a + b;
}
SRC

  local out3
  out3="$(run_backlog "$tmp" no)"
  if printf '%s\n' "$out3" | grep -q 'hardening | unsafe string/format API strcpy'; then
    printf 'cpp_backlog self-test: FAIL (strcpy row did not disappear after removal)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out3"
    exit 1
  fi
  if printf '%s\n' "$out3" | grep -q 'test-fuzz-coverage | parser/decoder entry point with no fuzz harness'; then
    printf 'cpp_backlog self-test: FAIL (parser/no-fuzz row did not disappear after removal)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out3"
    exit 1
  fi

  # JSON mode must be well-formed (an array) on the same tree.
  local js
  js="$(run_backlog "$tmp" yes)"
  case "$js" in
    \[*\]) : ;;
    *)
      printf 'cpp_backlog self-test: FAIL (JSON mode did not emit an array)\n'
      printf '%s\n%s\n' '--- json ---' "$js"
      exit 1
      ;;
  esac

  # -------------------------------------------------------------------------
  # F3a: a .github/workflows matrix must be DETECTED (no "no CI matrix" claim).
  # The dir is hidden, so this also locks the --hidden + **/ glob fix.
  # -------------------------------------------------------------------------
  mkdir -p "$tmp/.github/workflows"
  cat >"$tmp/.github/workflows/ci.yml" <<'YML'
name: ci
jobs:
  build:
    strategy:
      matrix:
        cc: [gcc, clang]
        arch: [x86_64, aarch64]
    runs-on: ubuntu-latest
YML
  local out_ci
  out_ci="$(run_backlog "$tmp" no)"
  if printf '%s\n' "$out_ci" | grep -q 'no CI matrix detected'; then
    printf 'cpp_backlog self-test: FAIL (blind to .github/workflows matrix, F3a)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_ci"
    exit 1
  fi
  if ! printf '%s\n' "$out_ci" | grep -q 'portability | CI matrix present'; then
    printf 'cpp_backlog self-test: FAIL (CI matrix not recognized, F3a)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_ci"
    exit 1
  fi
  if ! printf '%s\n' "$out_ci" | grep -qE 'CI matrix present.*\.github/workflows/ci\.yml'; then
    printf 'cpp_backlog self-test: FAIL (CI anchor not repo-relative, F3a)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_ci"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # F3b: a shipped fuzz harness that references a parser COVERS it (no row),
  # and the harness file itself is never flagged as an uncovered entry point.
  # -------------------------------------------------------------------------
  mkdir -p "$tmp/fuzz"
  cat >"$tmp/src/main.c" <<'SRC'
#include <string.h>
#include <stdlib.h>
int parse_packet(const unsigned char *buf, size_t len) {
    return (int)(buf[0] + len);
}
SRC
  cat >"$tmp/fuzz/fuzz_parse.c" <<'SRC'
#include <stddef.h>
#include <stdint.h>
int parse_packet(const unsigned char *buf, size_t len);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return parse_packet(data, size);
}
SRC
  local out_fuzz
  out_fuzz="$(run_backlog "$tmp" no)"
  if printf '%s\n' "$out_fuzz" | grep -qE 'test-fuzz-coverage \| parser/decoder.*\| src/main\.c'; then
    printf 'cpp_backlog self-test: FAIL (parser covered by fuzz harness still flagged, F3b)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_fuzz"
    exit 1
  fi
  if printf '%s\n' "$out_fuzz" | grep -qE 'test-fuzz-coverage \| parser/decoder.*\| fuzz/fuzz_parse\.c'; then
    printf 'cpp_backlog self-test: FAIL (fuzz harness file itself flagged as uncovered, F3b)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_fuzz"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # F1a (C++): on a repo WITH C++ sources, ptr+len advice recommends std::span/
  # string_view (not the C ownership-contract relabel).
  # -------------------------------------------------------------------------
  local cpptmp
  cpptmp="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp' '$cpptmp'" EXIT
  mkdir -p "$cpptmp/src"
  cat >"$cpptmp/CMakeLists.txt" <<'CM'
cmake_minimum_required(VERSION 3.16)
project(fakecpp CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(fakecpp src/main.cpp)
CM
  cat >"$cpptmp/src/main.cpp" <<'SRC'
#include <cstddef>
int sum_bytes(const char *buf, std::size_t len) {
    int t = 0;
    for (std::size_t i = 0; i < len; ++i) t += buf[i];
    return t;
}
SRC
  local out_cpp
  out_cpp="$(run_backlog "$cpptmp" no)"
  if ! printf '%s\n' "$out_cpp" | grep -q 'api-ergonomics | pointer+length parameter pair with no std::span/string_view'; then
    printf 'cpp_backlog self-test: FAIL (C++ repo did not get std::span advice, F1a)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_cpp"
    exit 1
  fi
  if printf '%s\n' "$out_cpp" | grep -q 'C: document the ptr+len'; then
    printf 'cpp_backlog self-test: FAIL (C ownership-contract relabel leaked onto a C++ repo, F1a)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_cpp"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # R1-mixed (per-FILE C++ gating): on a MIXED C/C++ repo the std::span advice is
  # chosen PER FILE — a ptr+len pair in a C++ TU gets span advice, the SAME shape in
  # a `.c`/`.h` file gets the W2 C ownership-contract relabel (zephyr 9413 .c span FPs).
  # -------------------------------------------------------------------------
  local mixedtmp
  mixedtmp="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp' '$cpptmp' '$mixedtmp'" EXIT
  mkdir -p "$mixedtmp/src" "$mixedtmp/arch"
  cat >"$mixedtmp/CMakeLists.txt" <<'CM'
cmake_minimum_required(VERSION 3.16)
project(mixed C CXX)
CM
  # A real C++ TU with a ptr+len pair -> span advice (the file makes the repo C++).
  cat >"$mixedtmp/src/view.cpp" <<'SRC'
#include <cstddef>
int sum_cpp(const char *buf, std::size_t len) { return (int)(buf[0] + len); }
SRC
  # A pure-C source with a ptr+len pair -> C ownership-contract relabel (NOT span).
  cat >"$mixedtmp/arch/cache.c" <<'SRC'
#include <stddef.h>
int sum_c(const char *buf, size_t len) { return (int)(buf[0] + len); }
SRC
  # A pure-C header with a ptr+len pair -> C ownership-contract relabel (NOT span).
  cat >"$mixedtmp/arch/mmu.h" <<'SRC'
#include <stddef.h>
int map_region(unsigned char *base, size_t count);
SRC
  local out_mixed
  out_mixed="$(run_backlog "$mixedtmp" no)"
  # R1-mixed.1: the C++ TU ptr+len pair gets std::span advice (no over-correction).
  if ! printf '%s\n' "$out_mixed" | grep -qE 'pointer\+length parameter pair with no std::span/string_view.*\| src/view\.cpp:[0-9]+'; then
    printf 'cpp_backlog self-test: FAIL (R1-mixed: C++ TU ptr+len did not get std::span advice)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_mixed"
    exit 1
  fi
  # R1-mixed.2: the C-file ptr+len pairs get the C ownership relabel, NOT span.
  if ! printf '%s\n' "$out_mixed" | grep -qE 'C: document the ptr\+len ownership/bounds contract.*\| arch/cache\.c:[0-9]+'; then
    printf 'cpp_backlog self-test: FAIL (R1-mixed: C-file ptr+len did not get the C ownership relabel)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_mixed"
    exit 1
  fi
  # R1-mixed.3: NO C-file (.c/.h) line may carry the std::span advice (the FP class).
  if printf '%s\n' "$out_mixed" | grep -qE 'no std::span/string_view.*\| arch/(cache\.c|mmu\.h):'; then
    printf 'cpp_backlog self-test: FAIL (R1-mixed: std::span advice leaked onto a .c/.h file on a mixed repo)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_mixed"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # R10: bare deps/, OSS-Fuzz fuzz/, and autosetup/jimsh0.c vendored trees are
  # excluded from the backlog lanes; a real shipped src/ hit in the same repo survives.
  # -------------------------------------------------------------------------
  local exclbltmp
  exclbltmp="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp' '$cpptmp' '$mixedtmp' '$exclbltmp'" EXIT
  mkdir -p "$exclbltmp/src" "$exclbltmp/deps/hiredis" "$exclbltmp/fuzz" "$exclbltmp/autosetup"
  cat >"$exclbltmp/src/core.c" <<'SRC'
#include <string.h>
void core(char *d, const char *s) { strcpy(d, s); }
SRC
  cat >"$exclbltmp/deps/hiredis/net.c" <<'SRC'
#include <string.h>
void vend(char *d, const char *s) { strcpy(d, s); }
SRC
  cat >"$exclbltmp/fuzz/decompress.c" <<'SRC'
#include <string.h>
void fz(char *d, const char *s) { strcpy(d, s); }
SRC
  cat >"$exclbltmp/autosetup/jimsh0.c" <<'SRC'
#include <string.h>
void jim(char *d, const char *s) { strcpy(d, s); }
SRC
  local out_exclbl
  out_exclbl="$(run_backlog "$exclbltmp" no)"
  if printf '%s\n' "$out_exclbl" | grep -qE 'deps/|fuzz/decompress|jimsh0\.c'; then
    printf 'cpp_backlog self-test: FAIL (R10: a deps/fuzz/jimsh0 path leaked into the backlog)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_exclbl"
    exit 1
  fi
  if ! printf '%s\n' "$out_exclbl" | grep -qE 'src/core\.c:[0-9]+'; then
    printf 'cpp_backlog self-test: FAIL (R10 over-correction: real shipped src/core.c hit dropped)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_exclbl"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # R6.a (OSS-Fuzz/CIFuzz integration): a project wired into continuous fuzzing
  # (a `.github/workflows/cifuzz.yml`) must NOT get per-entry "no fuzz harness"
  # rows — it emits ONE informational row instead (nlohmann_json had 39 false).
  # -------------------------------------------------------------------------
  local cifztmp
  cifztmp="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp' '$cpptmp' '$mixedtmp' '$exclbltmp' '$cifztmp'" EXIT
  mkdir -p "$cifztmp/src" "$cifztmp/.github/workflows"
  cat >"$cifztmp/.github/workflows/cifuzz.yml" <<'YML'
name: CIFuzz
on: [pull_request]
jobs:
  Fuzzing:
    runs-on: ubuntu-latest
    steps:
      - uses: google/oss-fuzz/infra/cifuzz/actions/build_fuzzers@master
YML
  cat >"$cifztmp/src/api.c" <<'SRC'
#include <stddef.h>
int json_parse(const unsigned char *buf, size_t len) { return (int)(buf[0] + len); }
int cbor_decode(const unsigned char *buf, size_t len) { return (int)(buf[0] + len); }
SRC
  local out_cifz
  out_cifz="$(run_backlog "$cifztmp" no)"
  # R6.a.1: NO per-entry "no fuzz harness" row on a CIFuzz-integrated project.
  if printf '%s\n' "$out_cifz" | grep -qE 'test-fuzz-coverage \| parser/decoder entry point with no fuzz harness'; then
    printf 'cpp_backlog self-test: FAIL (R6.a: per-entry no-fuzz row emitted on a CIFuzz-integrated project)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_cifz"
    exit 1
  fi
  # R6.a.2: the single informational integration row IS present.
  if ! printf '%s\n' "$out_cifz" | grep -qF 'project integrates continuous fuzzing'; then
    printf 'cpp_backlog self-test: FAIL (R6.a: CIFuzz integration note missing)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_cifz"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # R6.c (entry-point precision): on a repo with NO fuzzing integration, the lane
  # flags only PUBLIC ENTRY-POINT DEFINITIONS — not internal `static`/`DUK_LOCAL`
  # functions, not prototypes/declarations (`;`), and not call-sites in expressions.
  # -------------------------------------------------------------------------
  local r6tmp
  r6tmp="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp' '$cpptmp' '$mixedtmp' '$exclbltmp' '$cifztmp' '$r6tmp'" EXIT
  mkdir -p "$r6tmp/src"
  cat >"$r6tmp/src/codec.c" <<'SRC'
#include <stddef.h>
/* PUBLIC definition — MUST be flagged (no harness). */
int json_decode(const unsigned char *buf, size_t len) {
    return helper_decode(buf, len);
}
/* internal-linkage helper — must NOT be flagged */
static int internal_parse(const unsigned char *b, size_t n) { return (int)(b[0] + n); }
DUK_LOCAL int duk__base64_decode_helper(const unsigned char *b, size_t n) { return (int)(b[0] + n); }
/* prototype/declaration — must NOT be flagged */
int base64_decode(const unsigned char *buf, size_t len);
int run(const unsigned char *p, size_t n) {
    if (json_decode(p, n)) return 1;
    int r = cbor_decode(p, n);
    return r;
}
SRC
  local out_r6
  out_r6="$(run_backlog "$r6tmp" no)"
  # R6.c.1: the PUBLIC json_decode DEFINITION (codec.c:3) IS flagged.
  if ! printf '%s\n' "$out_r6" | grep -qE 'test-fuzz-coverage \| parser/decoder.*\| src/codec\.c:3$'; then
    printf 'cpp_backlog self-test: FAIL (R6.c: the public json_decode definition was not flagged)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_r6"
    exit 1
  fi
  # R6.c.2: the internal `static`/`DUK_LOCAL` helpers (codec.c:7/8) are NOT flagged.
  if printf '%s\n' "$out_r6" | grep -qE 'test-fuzz-coverage \| parser/decoder.*\| src/codec\.c:(7|8)$'; then
    printf 'cpp_backlog self-test: FAIL (R6.c: an internal static/DUK_LOCAL helper was flagged as an entry point)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_r6"
    exit 1
  fi
  # R6.c.3: the prototype/declaration (codec.c:10, ends with ;) is NOT flagged.
  if printf '%s\n' "$out_r6" | grep -qE 'test-fuzz-coverage \| parser/decoder.*\| src/codec\.c:10$'; then
    printf 'cpp_backlog self-test: FAIL (R6.c: a prototype/declaration was flagged as an entry point)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_r6"
    exit 1
  fi
  # R6.c.4: the call-sites (codec.c:4/12/13) are NOT flagged.
  if printf '%s\n' "$out_r6" | grep -qE 'test-fuzz-coverage \| parser/decoder.*\| src/codec\.c:(4|12|13)$'; then
    printf 'cpp_backlog self-test: FAIL (R6.c: a call-site was flagged as an entry-point definition)\n'
    printf '%s\n%s\n' '--- backlog ---' "$out_r6"
    exit 1
  fi

  printf 'cpp_backlog self-test: PASS\n'
  exit 0
}

# ---------------------------------------------------------------------------
# Arg handling.
# ---------------------------------------------------------------------------
main() {
  local repo="."
  local json="no"
  local repo_set="no"

  for arg in "$@"; do
    case "$arg" in
      --self-test)
        self_test
        ;;
      --json)
        json="yes"
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      --*)
        printf 'error: unknown option: %s\n' "$arg" >&2
        usage >&2
        exit 2
        ;;
      *)
        if [ "$repo_set" = yes ]; then
          printf 'error: unexpected extra argument: %s\n' "$arg" >&2
          exit 2
        fi
        repo="$arg"
        repo_set="yes"
        ;;
    esac
  done

  # Normalize a trailing slash so prefix stripping is exact.
  repo="${repo%/}"
  [ -n "$repo" ] || repo="."

  run_backlog "$repo" "$json"
}

main "$@"
