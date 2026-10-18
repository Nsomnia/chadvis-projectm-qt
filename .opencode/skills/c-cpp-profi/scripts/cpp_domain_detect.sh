#!/usr/bin/env bash
#
# cpp_domain_detect.sh - mechanize the Pack-Selection Procedure from
# references/DOMAIN-AGNOSTIC-MASTERY.md.
#
# It runs the per-pack signal greps over a repo and prints every matched domain
# pack with the repo-relative file:line (or anchor) that matched. A repo may
# match several packs -> all are printed (the strictest constraint wins; union
# their gates). When no pack matches it prints:
#
#   unknown-domain: build a pack from references/UNKNOWN-DOMAIN.md
#
# It is READ-ONLY: it never writes to the target repo. Output is deterministic:
# repo-relative paths only, LC_ALL=C sort, no timestamps, no $RANDOM, no absolute
# paths, so two runs on an unchanged tree byte-match.
#
# Usage:
#   cpp_domain_detect.sh [REPO] [--json]   (default REPO=.)
#   cpp_domain_detect.sh --self-test
#
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# SELF_DIR is BASH_SOURCE-relative so the script works from any cwd. It is not
# otherwise used, matching the sibling helpers' location discipline.
: "${SELF_DIR:?}"

usage() {
  cat <<'USAGE'
usage: cpp_domain_detect.sh [REPO] [--json]
       cpp_domain_detect.sh --self-test

Mechanizes the Pack-Selection Procedure in references/DOMAIN-AGNOSTIC-MASTERY.md:
runs the per-pack signal greps over a repo and prints every matched domain pack
with the repo-relative file:line that selected it (a repo may match several ->
all printed). Prints "unknown-domain: build a pack from references/UNKNOWN-DOMAIN.md"
when none match. READ-ONLY. Deterministic and reproducible: two runs byte-match.
USAGE
}

# ---------------------------------------------------------------------------
# rg guard + search base, mirroring the sibling helpers' exclusions so anchors
# line up across the helper family. The detector greps source/header AND build
# and CI/config files, so its glob set is wider than the code-only helpers.
# ---------------------------------------------------------------------------
rg_available() {
  command -v rg >/dev/null 2>&1
}

# rg over the repo, vendored/build trees AND non-shipped dirs (tests, docs,
# examples, benches, runners) excluded so detection runs on the project's own
# shipped code, not its test fixtures / docs / vendored frameworks (F2b).
# Case-insensitive (signals are matched loosely). Args: PATTERN REPO [MODE]
#
# MODE selects the glob set:
#   all  (default) - source/header AND build/config files. Toolchain signals
#                    (`-ffast-math`, `nvcc`, `CMAKE_*`) live in build files.
#   code           - source/header files only. Used for the parser and generic
#                    packs, whose signals (format names like `toml`/`csv`,
#                    `*_parse`, `*_init/*_free`) are *code* identity; in build
#                    files those same tokens are filenames/targets (e.g. a
#                    Makefile listing `tests/*.toml`) and would misclassify (F2b).
#
# F2c BUG FIX: the pattern is passed via `-e "$1"` (not positionally). Several
# pack regexes begin with a dash (the HPC pattern starts with `-ffast-math`);
# passed positionally, rg parses the leading `-f` as its own `--file` flag,
# errors out, and `--no-messages`/`|| true` silently swallow it -> the pack is
# never detected. `-e` (and `--` before the path) makes a leading-dash pattern
# a literal search term, restoring HPC/SIMD detection (cglm's 297 `_mm_`).
# Shared exclusion globs, mirroring cpp_risk_scan.sh / cpp_backlog.sh so anchors
# line up across the helper family. Adds testing/, extras/, suffix-named tests and
# flat-root harnesses (R3) on top of the existing dir excludes — so a repo's own
# test/bench/vendored-split code never contributes domain signal.
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
  --glob '!**/docs/**'
  --glob '!**/doc/**'
  --glob '!**/examples/**'
  --glob '!**/example/**'
  --glob '!**/bench/**'
  --glob '!**/benches/**'
  --glob '!**/benchmark/**'
  --glob '!**/benchmarks/**'
  --glob '!**/extras/**'
  --glob '!**/extra/**'
  --glob '!**/runners/**'
  # G9 (150-repo gauntlet): tutorial/ trees are example apps, not the repo's domain
  # — mongoose ships 75 copies of its 1 MB mongoose.c under tutorials/ (~75 MB),
  # which alone drove a 60s+ timeout. Excluded like examples/. (Kept in the risk/
  # backlog sibling scripts too.)
  --glob '!**/tutorials/**'
  --glob '!**/tutorial/**'
  # R10: vendored *runtime* dependencies and generated amalgam/aux trees that the
  # `_deps/`/`third_party/`/`vendor/` set missed: the bare `deps/` convention
  # (redis/git/php bundle libs there — hiredis/lua/jemalloc), `dependencies/`
  # (simdjson's benchmark deps), the `singleheader/` generated amalgam (simdjson,
  # alongside the existing `single_include/`), `autosetup/` build glue + its
  # `jimsh0.c` bootstrap Jim-Tcl amalgam (sqlite), and OSS-Fuzz `fuzz/`/`fuzzing/`
  # harness dirs whose `*.cc`/`*.cpp` decoders otherwise flip the C++ signal and add
  # codec noise (libjpeg). The backlog test-fuzz lane still finds harnesses via its
  # OWN explicit `**/fuzz/**` glob, so coverage detection is unaffected.
  --glob '!**/deps/**'
  --glob '!**/dependencies/**'
  --glob '!**/singleheader/**'
  --glob '!**/fuzz/**'
  --glob '!**/fuzzing/**'
  --glob '!**/autosetup/**'
  --glob '!**/jimsh0.c'
  # R13/R10: data tables are NOT code. Even though `txt` is no longer in the build
  # glob, drop data-table files outright so a renamed/relocated table (or the literal
  # `CMakeLists.txt` glob) can never count `UnicodeData.txt`/`CaseFolding.txt` "LETTER
  # SHA" rows, `*.tables`, or binary `*.dat` as a code signal.
  --glob '!**/*.tables'
  --glob '!**/*.dat'
  --glob '!**/Unicode*.txt'
  --glob '!**/UnicodeData.txt'
  --glob '!**/CaseFolding.txt'
  # R11: vendored-framework excludes anchored to VENDORED LOCATIONS only. The bare
  # `!**/catch2/**`/`!**/catch.hpp`/`!**/gtest/**`/`!**/gmock/**`/`!**/unity*`/
  # `!**/utest.h` globs were meant to skip an EMBEDDED copy of a test framework that
  # OTHER repos vendor — but they also excluded the framework's OWN shipped source
  # when the repo IS that framework (Catch2's `src/catch2/`, 289 files, blinded 3/4
  # gates → a wrong Compilers primary off CMake `Interpreter` glue). Anchoring each
  # to a vendored parent (`third_party`/`vendor`/`extern`/`external`/`_deps`/`deps`/
  # `test`/`tests`/`testing`) drops only embedded copies while leaving the
  # framework's own `src/`/`include/` scannable.
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/unity*'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/utest.h'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/catch.hpp'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/catch2/**'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/gtest/**'
  --glob '!**/{third_party,thirdparty,vendor,extern,external,_deps,deps,test,tests,testing}/**/gmock/**'
  --glob '!**/*_test.*'
  --glob '!**/*_tests.*'
  # G7 (150-repo gauntlet): anchored test-SOURCE forms replacing the old unanchored
  # `*test*.c` substring (which dropped real code like attestation.c); G6: test-only
  # HEADER conventions. Mirrors cpp_risk_scan.sh / cpp_backlog.sh.
  --glob '!**/test_*.c'
  --glob '!**/test_*.cc'
  --glob '!**/test_*.cpp'
  --glob '!**/test_*.cxx'
  --glob '!**/*_bench*.*'
  --glob '!**/ltests.*'
  --glob '!**/tests_impl.*'
  --glob '!**/tests_exhaustive*'
  --glob '!**/testrand*'
  --glob '!**/testutil.*'
  --glob '!**/tests_common.*'
  --glob '!**/unit_test.*'
  --glob '!**/wycheproof/**'
  # G10 (150-repo gauntlet): CMake find-modules are build infrastructure, NOT the
  # repo's domain — FindMbedTLS.cmake / FindOpenSSL.cmake etc. flooded the Crypto
  # pack and mis-primaried curl/libcoap/libwebsockets (Networking) as Crypto. Toolchain
  # detection reads CMakeLists.txt separately, so dropping Find*.cmake here is safe.
  --glob '!**/Find*.cmake'
  --glob '!**/cmake/Find*'
  # G11 (150-repo gauntlet): vendored/bundled dependency trees under non-standard
  # dir spellings the third_party/vendor set missed, plus SWIG-generated bindings —
  # mold (third-party/zstd -> Compression), mgba (src/third-party/sqlite3 -> Databases).
  --glob '!**/third-party/**'
  --glob '!**/3rdparty/**'
  --glob '!**/3rd_party/**'
  --glob '!**/*_wrap.c'
  --glob '!**/*_wrap.cc'
  --glob '!**/*_wrap.cxx'
  # R3+ test/vendored/generated conventions that path-segment + suffix globs miss
  # (kept identical to cpp_risk_scan.sh / cpp_backlog.sh so anchors line up): NASA
  # cFE `ut-coverage/`/`ut-stubs/`; CamelCase test roots (`STest/`, `FppTestProject/`,
  # any `[A-Z]*Test/` such as GTest/); the `*test_inc.h` driver-include (pcre2);
  # generated `single_include/` amalgamations (nlohmann); vendored target-libc
  # headers under `*/win32/include/` (tinycc mingw). These also keep a wrong domain
  # signal from a repo's test/vendored trees (fprime's Compilers anchor lived in
  # `FppTestProject/`). Ordinary public `include/` is NOT excluded — only the
  # `single_include/` mirror and `win32/include/` vendored variant.
  --glob '!**/ut-coverage/**'
  --glob '!**/ut-stubs/**'
  --glob '!**/STest/**'
  --glob '!**/*TestProject*/**'
  --glob '!**/[A-Z]*Test/**'
  --glob '!**/*test_inc.h'
  --glob '!**/single_include/**'
  --glob '!**/win32/include/**'
)

# Whole-file comment/string stripper (R2 + R13): emits "<cleaned>\t<path>:<line>:<orig>"
# for every line of the files passed as args, blanking C/C++ // /* */ comment
# spans (block state tracked across lines from each file's start) and "..."/'...'
# literal contents. The same helper used by the sibling scripts; a downstream rg
# re-applies the signal pattern to the CLEANED field so a token that lived only
# in a TRAILING //, an inline/▸multi-line /* */, or a string literal disappears
# (the F1b leading-marker-only filter missed all three). Build/CI files (YAML/
# CMake) carry their tokens in code, not C comments, so the strip is a near no-op
# there and never removes a real toolchain signal.
#
# R13: in a NON-C file (Makefile/YAML/sh/CMake/etc.) a `#` begins a comment to
# end-of-line and is blanked too — so a Makefile `@# SHA1: 774be8…` checksum
# comment and a YAML `# note` no longer feed the signal greps (this, plus the
# data-file skip, is what stops duktape's false Crypto primary). In a C/C++ file
# `#` is a PREPROCESSOR directive (`#pragma omp`, `#define X_IMPLEMENTATION`,
# `#include`) that carries real domain signal, so it is NEVER treated as a comment
# there. The per-file `hashcomment` flag is set at FNR==1 from the extension.
STRIP_COMMENTS_AWK='
FNR == 1 {
  inblock = 0; linecont = 0
  hashcomment = (FILENAME ~ /\.(c|cc|cpp|cxx|h|hh|hpp|hxx|cu|cuh|cl)$/) ? 0 : 1
}
maxln > 0 && FNR > maxln { next }
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
    if (hashcomment && c == "#") break
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

# Glob set for a mode: source/header always; build/config files added for 'all'.
# R13/R10: `txt` is intentionally NOT in the build-file wildcard. The only `.txt`
# that carries a toolchain signal is `CMakeLists.txt`, matched by its own literal
# glob below; a bare `*.txt` wildcard instead pulled in DATA TABLES as "code"
# matches (sqlite `ext/fts3/unicode/UnicodeData.txt`/`CaseFolding.txt`, duktape
# `src-input/UnicodeData.txt`) — the "CYRILLIC LETTER SHA" rows that handed duktape
# a false Crypto primary and sqlite spurious Filesystems/Crypto secondaries. Data
# tables (`*.txt`/`*.tables`/`Unicode*.txt`/`*.dat`) are belt-and-suspenders dropped
# in EXCLUDE_GLOBS too, so they never count even via the literal `CMakeLists.txt`.
signal_globs() {
  local mode="${1:-all}"
  printf '%s\0' '*.{c,cc,cpp,cxx,h,hh,hpp,hxx,cu,cuh,cl}'
  if [ "$mode" = all ]; then
    printf '%s\0' '*.{cmake,build,mk,json,yml,yaml,ld,ini}' \
                  'CMakeLists.txt' 'Makefile' 'GNUmakefile' \
                  'meson.build' 'Kconfig' 'Kbuild'
  fi
}

# Comment/string-stripped, mostly-case-insensitive signal matches for PATTERN under
# REPO. Returns "path:line:original" rows whose CODE part matches (R2/R3). Args: PATTERN REPO [MODE]
#
# R8: matching is case-insensitive by default (signals are matched loosely), but a
# pack may force a DISTINCTIVE token case-sensitive with an inline `(?-i:...)` group
# so its uppercase-API vocabulary matches the real domain but NOT ordinary
# identifiers that merely share the spelling. The SPACE pack's `OS_`/`CFE_`/`OS_API`
# and the Compilers pack's `opcode`/`OPCODE` use this: without it the case-folded
# `OS_[A-Z]` matched zlib's gzip-header `OS_CODE` (+ `os_flush`) → SPACE-primary on a
# compression lib, and `\bopcode\b` matched fprime's CamelCase `FwOpcodeType` →
# Compilers over Space. Because `(?-i:...)` (and the `OS_(?!CODE\b)` negative
# lookahead the SPACE pack uses to drop the gzip token) are PCRE constructs that
# rg's DEFAULT regex engine rejects, BOTH the file-list pass and the re-match pass
# run under PCRE (`-P`); the patterns are plain alternations/`\b`/char-classes that
# the re-match pass already required to be PCRE-valid, so this is semantics-stable.
rg_signal() {
  local mode="${3:-all}"
  # G9 (150-repo gauntlet) large-repo FAST PATH: when run_detect has pre-stripped
  # the repo into a bounded, comment/string-cleaned corpus (huge or amalgam repos —
  # nuttx 16.9k files, mongoose's 75 mongoose.c copies), re-match the signal against
  # the cleaned column ONCE-stripped, instead of re-listing + re-stripping per pack
  # (O(packs*files), which exit-124'd at 60s). Normal repos leave the corpus globals
  # empty and take the exact original path below — their calibrated output is
  # byte-identical (this branch is inert for them).
  local corpus=""
  case "$mode" in
    code) corpus="${DOMAIN_CORPUS_CODE:-}" ;;
    *)    corpus="${DOMAIN_CORPUS_ALL:-}" ;;
  esac
  if [ -n "$corpus" ]; then
    rg -iP --no-messages "^[^\t]*(?:$1)" "$corpus" 2>/dev/null | cut -f2- || true
    return 0
  fi
  local gl=()
  local g
  while IFS= read -r -d '' g; do gl+=(--glob "$g"); done < <(signal_globs "$mode")
  local files
  files="$(rg -liP -l --no-messages \
      "${gl[@]}" \
      "${EXCLUDE_GLOBS[@]}" \
      -e "$1" -- "$2" 2>/dev/null | LC_ALL=C sort || true)"
  [ -n "$files" ] || return 0
  printf '%s\n' "$files" | awk 'NF' | tr '\n' '\0' \
    | xargs -0 awk "$STRIP_COMMENTS_AWK" 2>/dev/null \
    | rg -iP "^[^\t]*(?:$1)" 2>/dev/null \
    | cut -f2- || true
}

# G9: caps for the large-repo corpus (override via env). Trigger when a repo's
# source-file count exceeds DOMAIN_FILE_CAP; then scan a bounded sample: at most
# DOMAIN_BASENAME_CAP copies of any basename, DOMAIN_FILE_CAP files total, and the
# first DOMAIN_LINECAP lines of each (so a 1 MB single-file amalgam cannot dominate).
# G9 caps (override via env). Corpus mode triggers when a repo exceeds DOMAIN_FILE_CAP
# files OR DOMAIN_BYTE_CAP bytes of source; it then scans a bounded, EVENLY-SAMPLED
# corpus: <=DOMAIN_BASENAME_CAP copies of any basename, ~DOMAIN_SAMPLE files spread
# uniformly across the (sorted) tree (NOT the first N — that would bias a big repo to
# its alphabetically-first subtree), each truncated to DOMAIN_LINECAP lines. These
# bounds make a 17k-file RTOS or an 8 MB amalgam finish in seconds; a normal repo
# never enters corpus mode and keeps its exact calibrated per-pack scan.
: "${DOMAIN_FILE_CAP:=6000}"
: "${DOMAIN_BASENAME_CAP:=4}"
: "${DOMAIN_LINECAP:=4000}"
: "${DOMAIN_BYTE_CAP:=6000000}"
: "${DOMAIN_SAMPLE:=1000}"
DOMAIN_CORPUS_ALL=""
DOMAIN_CORPUS_CODE=""

# Strip a bounded EVEN SAMPLE of the repo's source ONCE into a comment/string-cleaned
# corpus that rg_signal then re-matches per pack. The line cap is enforced via maxln
# in STRIP_COMMENTS_AWK. Args: REPO MODE OUTFILE.
build_domain_corpus() {
  local repo="$1" mode="$2" out="$3"
  local gl=() g
  while IFS= read -r -d '' g; do gl+=(--glob "$g"); done < <(signal_globs "$mode")
  local flist total step
  flist="$( { rg --files --no-messages "${gl[@]}" "${EXCLUDE_GLOBS[@]}" -- "$repo" 2>/dev/null || true; } \
      | LC_ALL=C sort \
      | awk -F/ -v k="${DOMAIN_BASENAME_CAP}" '{ b = $NF; if (++seen[b] <= k) print }')"
  total="$(printf '%s\n' "$flist" | awk 'NF' | wc -l | tr -d ' ')"
  step=$(( total / DOMAIN_SAMPLE + 1 ))
  # (NR-1) % step == 0 keeps every step-th file starting at the first; with step==1
  # (fewer files than the sample target, e.g. wolfssl's 767 large files) it keeps
  # ALL of them — `NR % step == 1` would select NONE when step==1 (NR%1 is always 0).
  printf '%s\n' "$flist" | awk 'NF' | awk -v s="$step" '(NR - 1) % s == 0' \
    | tr '\n' '\0' \
    | xargs -0 -r awk -v maxln="${DOMAIN_LINECAP}" "$STRIP_COMMENTS_AWK" 2>/dev/null \
    > "$out" 2>/dev/null || true
}

# Strip a leading "REPO/" (or "REPO" == ".") prefix so anchors are repo-relative.
# Args: REPO  (reads file:line tokens on stdin, rewrites the path component)
strip_repo_prefix() {
  local repo="$1"
  local prefix="$repo/"
  awk -v prefix="$prefix" '
    {
      n = index($0, ":")
      if (n == 0) { print; next }
      path = substr($0, 1, n - 1)
      rest = substr($0, n)
      sub(/^\.\//, "", path)
      if (substr(path, 1, length(prefix)) == prefix) {
        path = substr(path, length(prefix) + 1)
      }
      print path rest
    }'
}

# Repo-relative, comment/string-stripped, de-duplicated file:line anchors for
# PATTERN under REPO, LC_ALL=C sorted (rg_signal already strips comments/strings
# and re-matches, so a token that lived only in a comment/literal is gone). Args:
# PATTERN REPO [MODE]
pack_anchors() {
  rg_signal "$1" "$2" "${3:-all}" \
    | strip_repo_prefix "$2" \
    | awk -F: 'NF>=2 { print $1 ":" $2 }' \
    | LC_ALL=C sort -u
}

# ---------------------------------------------------------------------------
# Pack table. Each pack is "id|label|signal-regex". The regex is the union of
# the pack's Pack-Selection signals from DOMAIN-AGNOSTIC-MASTERY.md. Order is
# fixed so output is deterministic.
# ---------------------------------------------------------------------------
# Real-domain packs are listed first; `generic` is the honest fallback for a
# real C/C++ library that matches no domain and is ranked/handled specially in
# detect() so it never masks a genuine domain match.
pack_ids() {
  printf '%s\n' \
    space embedded kernel gpu hpc crypto networking compression \
    compilers linker emulator databases audio graphics profiler filesystems assetloader parser generic
}

pack_label() {
  case "$1" in
    space)       printf 'Space / satellites' ;;
    embedded)    printf 'Embedded / real-time' ;;
    kernel)      printf 'Kernel / drivers' ;;
    gpu)         printf 'GPU (CUDA / SYCL / HIP)' ;;
    hpc)         printf 'HPC / SIMD / numerics' ;;
    crypto)      printf 'Crypto' ;;
    networking)  printf 'Networking / protocols' ;;
    compression) printf 'Compression / codec' ;;
    compilers)   printf 'Compilers / interpreters / VMs' ;;
    linker)      printf 'Linker / binary tools / object formats' ;;
    emulator)    printf 'Emulator / hardware & ISA emulation' ;;
    databases)   printf 'Databases / storage engines' ;;
    audio)       printf 'Audio / DSP / real-time media' ;;
    graphics)    printf 'Graphics / windowing / native UI / rendering' ;;
    profiler)    printf 'Benchmarking / profiling' ;;
    filesystems) printf 'Filesystems / block storage' ;;
    assetloader) printf '3D asset / model-format import (glTF/OBJ/FBX/COLLADA)' ;;
    parser)      printf 'Parser / text-format / serialization' ;;
    generic)     printf 'Generic library / data-structures / strings' ;;
    *)           printf 'unknown' ;;
  esac
}

# Signal regex per pack. Patterns are deliberately conservative: they mirror the
# documented signals and avoid tokens that fire on unrelated code. Matching is
# case-insensitive by default; a DISTINCTIVE token is wrapped in an inline
# `(?-i:...)` group so it is matched CASE-SENSITIVELY (R8/R9-vocab):
#   - SPACE: `cFE_`/`CFE_`/`OS_API`/`OS_*` (with `OS_(?!CODE\b)` dropping zlib's
#     gzip-header `OS_CODE`) plus F´/F-Prime `CCSDS`/`Framer`/`Deframer`/`Tlm`/
#     `APID`/`FwOpcode`/`CmdResponse` (case-sensitive so they match fprime's flight
#     vocabulary but not prose), and Compilers' `opcode`/`OPCODE` — so cFS/F´/compiler
#     vocabulary matches but ordinary identifiers (zlib `OS_CODE`/`os_flush`, fprime's
#     CamelCase `FwOpcodeType`) do not.
#   - COMPRESSION: `LZ4_`/`LZ77`/`ZSTD_` are case-sensitive (the codec API spelling),
#     while `deflate`/`inflate`/`compress`/`crc32`/`adler32`/`gzip` stay loose.
#   - NETWORKING: the distinctive nouns (`socket`/`listener`/`dialer`/`endpoint`/
#     `sockaddr`) stay loose, but the generic syscall verbs `connect`/`bind`/`accept`/
#     `send`/`recv`/`sendto`/`recvfrom`/`poll`/`epoll` are case-sensitive LOWERCASE
#     (the POSIX socket convention) so they match real socket I/O but NOT CamelCase
#     methods that merely share the spelling (tinyxml2's visitor `Accept()`).
#   - CRYPTO: the bare `hash` token is NOT used (it fires on hashtable containers —
#     klib/uthash/lua); `hash` only counts inside a crypto context
#     (`crypto`/`sha`/`blake`/`hmac` prefix or a `256`/`512`/`sha` suffix).
pack_regex() {
  case "$1" in
    space)       printf '%s' '\bMISRA\b|rules? of ten|Power of Ten|(?-i:\bcFE_|\bCFE_|\bOS_(?:API|printf|[A-Z][a-z])|\bCCSDS\b|\bFramer\b|\bDeframer\b|\bTlm\b|\bAPID\b|\bFwOpcode|\bCmdResponse)|\btelemetry\b|\bspacecraft\b|\bwatchdog\b|\bRTEMS\b|EXPORT_SYMBOL_NASA' ;;
    embedded)    printf '%s' '\bFreeRTOS\b|\bZephyr\b|xTaskCreate|-ffreestanding|\bvolatile\b.*(0x[0-9A-Fa-f]+|register)|ISR_HANDLER|\bHAL_' ;;
    kernel)      printf '%s' '(^|[^A-Za-z0-9_])__user([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])copy_(to|from)_user([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])MODULE_LICENSE([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])EXPORT_SYMBOL([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])spin_lock([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])GFP_(KERNEL|ATOMIC)([^A-Za-z0-9_]|$)' ;;
    gpu)         printf '%s' '(^|[^A-Za-z0-9_])__global__([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])__device__([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])__syncthreads([^A-Za-z0-9_]|$)|\bcudaMalloc\b|\bcudaMemcpy\b|\bhipMalloc\b|sycl::|-fsycl|\bhipError_t\b|\bhipStream_t\b|\bhipEvent_t\b|\bhipMemcpyKind\b|\bhipMemcpyHostToDevice\b|\bhipMemcpyDeviceToHost\b|\bhipDeviceptr_t\b|\bhipModule_t\b|\bhipFunction_t\b|\bhipDeviceProp_t\b|__HIP_PLATFORM_AMD__|\b__HIPCC__\b|defined\(__HIP__\)' ;;
    hpc)         printf '%s' '-ffast-math|_mm_[a-z]|vld1|svptrue|Eigen/|highway|#pragma omp|<cfenv>' ;;
    crypto)      printf '%s' 'constant.time|secret-dependent|\bEVP_|crypto_[a-z]|explicit_bzero|memset_s|\bFIPS\b|test.vector|\bhmac\b|\baead\b|\bblake|\bsha[0-9]?\b|\bsha3\b|\bmd5\b|\bchacha\b|\bpoly1305\b|\bkeystream\b|\bnonce\b|\bdigest\b|\bcipher\b|\baes\b|\bgcm\b|\bccm\b|\brsa\b|\becdsa\b|\becdh\b|\becdhe\b|\bx509\b|\bpkcs\b|\bpsk\b|\bcamellia\b|\bblowfish\b|\baria\b|\bsalsa20\b|\bcurve25519\b|\bed25519\b|\bsiphash\b|\bencrypt\b|\bdecrypt\b|\bciphertext\b|\bplaintext\b|\bkeypair\b|(crypto|secure|keyed|message|one.?way)[a-z0-9_]*hash|hash[a-z0-9_]*(256|512|384|224|md5|sha)|(sha|blake|md5|hmac|keccak)[a-z0-9_]*hash' ;;
    networking)  printf '%s' '\bntohl\b|\bhtons\b|\bntohs\b|\bhtonl\b|parse_packet|RFC[0-9]|__attribute__.*packed|#pragma pack|\bsocket\b|\blistener\b|\bdialer\b|\bsockaddr|\bsetsockopt\b|\bgetsockopt\b|\bendpoint\b|\btransport\b|(?-i:\bconnect\b|\bbind\b|\baccept\b|\bsend\b|\brecv\b|\bsendto\b|\brecvfrom\b|\bpoll\b|\bepoll\b)' ;;
    # COMPRESSION (R9-vocab + codec-WRAPPER vocab): the codec ENTROPY/stream core
    # (`deflate`/`inflate`/`huffman`/`LZ4_`/`crc32`/`adler32`) PLUS the container/
    # wrapper vocabulary a real image/byte-stream codec carries — without the wrapper
    # tokens a PNG codec (libpng) mis-primaried HPC off its few NEON `vld1` filter
    # files while the codec itself was invisible to the pack. PNG chunk names
    # (`IDAT`/`IHDR`/`IEND`/`PLTE`) are case-sensitive (the 4-byte FourCC spelling),
    # the wrapper verbs (`zstream`/`png_inflate`/`png_deflate`/`unfilter`/`filter_row`/
    # `scanline`/`png_palette`/`png_read`/`png_write`) stay loose. These also fire on
    # other byte-stream codecs (the `*_inflate`/`*_deflate`/`scanline`/`palette` idiom).
    compression) printf '%s' '\bdeflate\b|\binflate\b|\binflateBack\b|(?-i:\bLZ4_|\bLZ77\b|\bZSTD_|\bIDAT\b|\bIHDR\b|\bIEND\b|\bPLTE\b|\bVP8\b|\bVP8L\b|\bWEBP\b|\bMCU\b|\bjp2\b|\bJP2\b)|\blz4\b|\bzstd\b|\bhuffman\b|\bcompress\b|\buncompress\b|\bdecompress\b|\bgzip\b|\bgzopen\b|\bcrc32\b|\badler32\b|literal.length|sliding.window|\bzstream\b|[a-z]+_inflate\b|[a-z]+_deflate\b|\bunfilter|\bfilter_row\b|\bscanline\b|[a-z]+_palette\b|png_read_|png_write_|\bIDCT\b|\bFDCT\b|\bYCbCr\b|\bYCCK\b|\bchroma\b|\bmacroblock\b|\bcodestream\b|\bopj_|\bjpeg_|\bjpeg2000\b|\bcinfo\b|\bdjpeg\b|\bcjpeg\b' ;;
    compilers)   printf '%s' 'LLVMContext|llvm::|IRBuilder|emitOpcode|(?-i:\bopcode\b|\bOPCODE\b)|\bbytecode\b|interpreter|codegen|opt -verify|\bluaV_|\bluaK_|\bluaD_|\bProto\b|\bInstruction\b|\bvmcase\b|\bvmdispatch\b|\bdumpvar\b|\bast_node\b|\bcompile_stmt\b' ;;
    # G24 (150-repo gauntlet): linker pack uses ONLY linker-IMPLEMENTATION tokens
    # (mold-style *Section C++ class-name suffixes + get_*_addr accessors). The ELF-ABI
    # tokens (R_*, Elf_* structs, SHT_/SHF_/STT_/STB_/SHN_, st_shndx, section-name
    # strings, gotplt/dynsym) were DROPPED by the collision audit: they also fire on an
    # OS that LOADS ELF (nuttx ships a multi-arch ELF loader) and on stb_* macros
    # (raylib), so no ELF-ABI regex separates "linker" from "ELF consumer". This narrow
    # set flips mold from a wrong Parser-primary to a correct Linker-primary at zero
    # control risk; it will not (over-)claim tinycc/stella, which is the accepted price.
    linker)      printf '%s' '\b(?:Got|Plt|GotPlt|RelPlt|Dynsym|Symtab|MergedSection|OutputSection|InputSection)Section\b|\bget_(?:got|plt|gotplt|tlsgd)_addr\b' ;;
    # G24: emulator/ISA-emulation pack, grounded in mgba/stella/dosbox. Collision-pruned:
    # dropped MBC[0-9] (nuttx memory-bank-controller regs) and VRAM (raylib/nuttx),
    # narrowed IO_Read/IO_Write to case-sensitive (Lua io_read/io_write collided), and
    # poke to memory-poke function shapes (bare "poke" is prose). Surfaces as a strong,
    # correct SECONDARY on emulators (zero hits on any control); the over-broad Parser/
    # Networking packs still out-count it for PRIMARY (a separate documented residual).
    emulator)    printf '%s' '\bcartridge\b|\bbankswitch|\bIWRAM\b|(?-i:\bIO_Read[BWD]?\b)|(?-i:\bIO_Write[BWD]?\b)|\bCPU_Cycles\b|\bcyclesLate\b|RaiseIRQ|\bphys_page|\bvblank\b|\bhblank\b|\b(?:poke(?:8|16|32|b|w|l)|(?:mem|bus|io|cpu|ram|reg)_poke|poke_(?:byte|word|mem|addr))\b' ;;
    databases)   printf '%s' '\bfsync\b|fdatasync|write-ahead|write.?ahead|\bWAL\b|\bMVCC\b|crash.consistency|page_checksum|page.?cache|\bpwrite\b|dm-flakey|\bALICE\b|\bsqlite3?\b|\bbtree\b|b-tree|\bpager\b|\bvdbe\b|opcode.*VDBE|\browid\b|(?-i:\bPRAGMA\b|\bPragma)|\bvacuum\b|\bredis\b|\bredisDb\b|\brobj\b|\bRDB\b|\bAOF\b|\brdbSave\b|\brdbAdd|\bkeyspace\b|dict.*entry|\btransaction\b|\bcommit\b|\brollback\b|\bcompaction\b|\bmemtable\b|\bsstable\b|\bmanifest\b|\bsnapshot\b|write.?batch' ;;
    audio)       printf '%s' 'audio_?callback|audio_?buffer|process_block|\bdenormal|\bxrun\b|\bjack_|kAudioUnit|\bVST3\b|\bASIO\b|\bCoreAudio\b|flush.to.zero|samples?_per_(buffer|frame)' ;;
    # G24 (iter42): Graphics/windowing/native-UI pack — the skill had NO graphics pack
    # so glfw/SDL/raylib/sokol/bgfx/imgui/nanovg/Nuklear/nanogui all mis-primaried. Token
    # set is collision-audited to 0 hits on 24+ non-graphics controls (the raw gl*/GL_*
    # primitive tokens were DROPPED — they fire on zstd/mold OpenGL demos; InitWindow
    # dropped — cuda-samples). API/handle spellings are word-bounded in (?-i:...) groups.
    # Flips glfw/bgfx/nanovg/Nuklear/nanogui to Graphics-primary; SDL/raylib/sokol/imgui
    # get a correct Graphics SECONDARY (their primary-making GL-primitive tokens collided).
    # iter43 graphics-PRIMARY promotion: append the four repos' OWN library-NAMESPACE
    # identity tokens (NOT gl*/GL_* primitives — they wrongly flip dosbox/nuttx/glew —
    # and NOT the public SDL_[A-Z] surface consumers call — it collides on dosbox/mgba/
    # FFmpeg). These fire only on the libraries themselves and flip SDL/raylib/sokol/imgui
    # from Generic/Audio/Compression to Graphics-PRIMARY at zero control risk. The sokol
    # clause deliberately OMITS `_sg_[a-z]` (it substring-matched `_sg_reduce` WebGPU
    # subgroup-shader strings in ggml/llama.cpp/whisper.cpp — an ML control); the surviving
    # `_sapp_[a-z]` is sokol-unique. (Collision audit: 0 hits on any listed control repo.)
    graphics)    printf '%s' '(?-i:\bglfwCreateWindow\b|\bGLFWwindow\b|\bglfwPollEvents\b|\bglfwSwapBuffers\b|\bglfwMakeContextCurrent\b|\bglfwSwapInterval\b)|(?-i:\bSDL_CreateWindow\b|\bSDL_GL_SwapWindow\b|\bSDL_GL_CreateContext\b|\bSDL_CreateRenderer\b)|(?-i:\bsg_make_buffer\b|\bsg_apply_pipeline\b|\bsg_begin_pass\b|\bsg_make_shader\b|\bsapp_run\b)|bgfx::|(?-i:\bnvgBeginFrame\b|\bnvgBeginPath\b|\bNVGcontext\b)|\bnk_begin\b|\bnk_layout_row\b|\bBeginDrawing\b|\bEndDrawing\b|\bClearBackground\b|\brlglInit\b|(?-i:\bVkSwapchainKHR\b|\bvkCreateSwapchainKHR\b|\bVkRenderPass\b|\bVkCommandBuffer\b|\bVkPipelineLayout\b)|(?-i:\bRLAPI\b|\bTRACELOG\b|\brlgl[A-Z]|\brlEnable[A-Z]|\brlDisable[A-Z]|\brlLoad[A-Z]|\brlSetUniform)|(?-i:\bSOKOL_API_IMPL\b|\b_SOKOL_PRIVATE\b|\bSOKOL_ASSERT\b|\b_sapp_[a-z])|(?-i:\bImGui[A-Z]|\bImVec[234]\b|\bImDraw[A-Z]|\bImFont[A-Z]|\bIMGUI_API\b|\bImU32\b|\bImGuiContext\b)|(?-i:\bSDL_VideoDevice\b|\bSDL_Renderer\b|\bSDL_RenderCommand|\bSDL_WindowData\b|\bSDL_GLContext\b|\bSDL_GPUDevice\b|\bSDL_GPU[A-Z]|\bSDL_Texture\b|\bSDL_Surface\b|\bSDL_DisplayMode\b|\bSDL_VideoData\b|\bSDL_RenderDriver\b|\bSDL_DisplayData\b)' ;;
    # G24 (iter42): Benchmarking/profiling pack — gperftools tokens PLUS google/benchmark's
    # IMPLEMENTATION-INTERNAL symbols. The framework's PUBLIC API (`benchmark::State`/
    # `BENCHMARK_MAIN`/`DoNotOptimize`) was DEFERRED and stays out: those are shared infra
    # that collide on every repo that USES google/benchmark (abseil/json/leveldb/re2/rocksdb/
    # snappy/spdlog/xtensor + Catch2/gemmlowp/highway) — they are indistinguishable between
    # "I AM the framework" and "I USE the framework". The benchmark[detect] audit (workspace/
    # loop) instead added the framework's own engine internals — its registry
    # (`BenchmarkFamilies`/`RegisterBenchmarkInternal`), runner (`BenchmarkRunner`), reporter
    # hierarchy (`BenchmarkReporter`), colorprint (`ColorPrintf`/`ColorParseFormatLength`) and
    # sysinfo (`GetCacheSizesFromKVFS`/`GetNumCPUsImpl`) symbols. These live ONLY in google/
    # benchmark's own src/ (107 matches / 14 files) and an API CONSUMER never writes them: a
    # full 150-repo sweep with the standard vendored/bench/test excludes leaks them on ZERO
    # non-benchmark repos (vs. the API tokens which leaked on 6, abseil alone 50 files). They
    # `\bProfilerStart\b` still excludes cudaProfilerStart. VERIFIED (independent, path-arg
    # mode): gperftools -> Profiling PRIMARY; 0 on jemalloc/abseil and every other control,
    # incl. the abseil/json/leveldb/re2/rocksdb/snappy/spdlog/xtensor repos that USE google/
    # benchmark (they write the public API, never these engine internals). CAVEAT (honest):
    # google/benchmark THE REPO still self-excludes under `cpp_domain_detect.sh <path-ending-
    # in-/benchmark>` because `!**/benchmark/**` matches its own root dir name (the engine
    # tokens here DO classify it when run from INSIDE the repo as `... .`). That single-repo
    # dir-name quirk is a documented residual, NOT a domain gap — the profiling DOMAIN is
    # covered via gperftools. Engine tokens kept: collision-free, add signal from-inside.
    # iter43 collision hardening: `BenchmarkReporter` is word-anchored `\b...\b` so it no
    # longer substring-matches Catch2's user class `CumulativeBenchmarkReporter` (unit-test
    # domain, distinct from Google-Benchmark microbench) — that Catch2 file also lives under
    # tests/ so it was already excluded, but the anchor is correct defense-in-depth. The dead
    # token `ColorParseFormatLength` (0 hits anywhere, including the benchmark target) was
    # dropped as it contributes no signal.
    profiler)    printf '%s' '\bProfilerStart\b|\bProfilerEnable\b|\bProfilerFlush\b|\bProfilerRegisterThread\b|\bHeapProfilerStart\b|\bIsHeapProfilerRunning\b|\bHeapProfilerDump\b|\bMallocExtension\b|\bBenchmarkRunner\b|\bBenchmarkFamilies\b|\bBenchmarkReporter\b|\bRegisterBenchmarkInternal\b|\bColorPrintf\b|\bGetCacheSizesFromKVFS\b|\bGetNumCPUsImpl\b' ;;
    # FILESYSTEMS: `barrier` is word-BOUNDED (`\bbarrier\b`) so it matches a genuine
    # I/O / write barrier but NOT a GC write-barrier identifier like lua's
    # `luaC_barrier`/`luaC_barrierback` (the leading `_` is a word char, so there is
    # no `\b` before `barrier` there). Without the boundary the bare token matched 44
    # lua GC calls → lua mis-primaried Filesystems over its real Compilers/VM domain.
    filesystems) printf '%s' '\bsuperblock\b|\binode\b|on-disk|\bmount\b|\bfsck\b|dm-flakey|\bFUA\b|crash.injection|\bbarrier\b|write.?barrier' ;;
    # PARSER (+ CamelCase serialization vocab): the snake_case `*_parse`/`parse_*`
    # idiom PLUS the CamelCase `ParseXxx`/`SkipWhitespace`/`kParseError*` vocabulary a
    # header-only C++ serializer uses (rapidjson) — case-sensitive `(?-i:...)` so it
    # matches `ParseValue`/`ParseString`/`SkipWhitespace`/`kParseFlag` but NOT a codec's
    # lowercase `decode`/`partial_decode` (which would steal a codec into Parser). Before
    # this, rapidjson scored only 11 Parser (its core uses CamelCase, not `*_parse`) and
    # mis-primaried HPC off its 2 SIMD scan headers.
    # iter43 ASSET-LOADER pack: 3D model-format import (tinyobjloader/cgltf/assimp). These
    # mis-primaried as generic Parser, but the domain carries hazards a text-format Parser
    # does not model: bounds-checked accessor/bufferView indexing into a BINARY buffer
    # (cgltf `req_size = offset + stride*(count-1) + element_size` validated vs buffer length
    # before the pointer add), mesh-topology/index validation (face indices within vertex/
    # normal/texcoord array bounds), untrusted-asset fuzzing of every decoder, and coordinate-
    # system/handedness/winding/unit handling. Ranked AHEAD of parser/generic so the more-
    # specific domain wins a count tie. Token set is the collision-audited API spellings;
    # `shape_t`/`attrib_t` were EXCLUDED as fundamentally ambiguous (shape_t = tensor-shape in
    # ncnn/xtensor; attrib_t = a POSIX fs-attr struct in libuv). Residual hits on bgfx(8)/
    # raylib(26)/bullet3(1) are LEGITIMATE embedded loaders (those repos vendor cgltf/tinyobj)
    # and land as a correct weak SECONDARY, dwarfed by each repo's own primary.
    assetloader) printf '%s' '(?-i:\btinyobj::|\bLoadObj\b)|\bcgltf_parse\b|\bcgltf_data\b|\bcgltf_accessor\b|\bcgltf_buffer_view\b|\bcgltf_mesh\b|\bcgltf_primitive\b|\bcgltf_node\b|\bcgltf_skin\b|\bcgltf_animation\b|(?-i:\baiScene\b|\baiMesh\b|\baiNode\b|\baiImporter\b|\baiMaterial\b|\baiAnimation\b|\baiTexture\b)|Assimp::Importer' ;;
    parser)      printf '%s' '\b(json|xml|yaml|toml|ini|csv|protobuf|msgpack|riff|fourcc)\b|\bhttp_?(parse|request|response)|[a-z0-9]+_parse\b|\bparse_[a-z]|\btokeniz|\blexer\b|\bgrammar\b|\b(json|xml|yaml|toml|http|url|base64|hex|utf8?|token|field|header|message)[a-z0-9]*_decode\b|\bdeserialize\b|\bphr_|\byy(parse|lex|_)|\bdr(wav|flac|mp3)_|(?-i:\bParse[A-Z][a-z]|\bSkipWhitespace\b|\bkParse[A-Z])' ;;
    generic)     printf '%s' '\bKHASH|\bkh_init\b|\bkvec_t\b|kv_(init|push)\b|\bUT_hash|HASH_(ADD|FIND|DEL)\b|stb_ds|\bsds[a-z]+\(|typedef +struct[^;]*\{|[A-Za-z][A-Za-z0-9]*_(init|free|destroy|new)\b|#define +[A-Z0-9_]*IMPLEMENTATION' ;;
    *)           printf '%s' 'a^' ;;   # never matches
  esac
}

# Sort priority tier (F2b ranking). gpu/kernel carry unambiguous, never-incidental
# signatures (`__global__`, `MODULE_LICENSE`, `copy_from_user`) so they always
# outrank the generic fallback regardless of count — this protects real CUDA /
# kernel detection. Every other pack (including generic) shares the base tier and
# is then ranked by code-match count. Higher tier sorts first.
pack_priority() {
  case "$1" in
    gpu|kernel|emulator) printf '1' ;;
    *)          printf '0' ;;
  esac
}

# Search mode per pack. The parser and generic packs match *code* identity
# (format names, idiomatic API/macro shapes) that appears as filenames/targets
# in build files, so they scan source/headers only; every other pack also wants
# its toolchain signals from build files and uses the wide ('all') set.
pack_mode() {
  case "$1" in
    parser|generic|linker|graphics|assetloader) printf 'code' ;;
    *)                     printf 'all' ;;
  esac
}

# ---------------------------------------------------------------------------
# Detection (R5 ranked): count each pack's distinct, comment/string-stripped code
# matches, then RANK. Emits tab-separated "id\tlabel\tanchor\tcount\trole" rows.
# Roles: primary (the binding classification) / secondary (other confident packs).
# Packs with a single incidental code match (count==1) are dropped (F2b).
#
# Ranking is primarily by COUNT, with two corrections proven necessary on the
# batch-2 repos (R5):
#  (b) TIER-1 COUNT FLOOR. gpu/kernel carry unambiguous, never-incidental tokens
#      (`__global__`, `MODULE_LICENSE`, `copy_from_user`) and sit in priority tier
#      1 so a genuine CUDA/Linux-driver repo outranks the broad generic pack. But
#      a tier-1 pack must EARN that promotion: it only jumps ahead of a
#      higher-count tier-0 pack when its own count clears a floor (>=3) AND is
#      within 10x of the leader. This stops 8 incidental GPU substring hits from
#      beating 1446 Audio matches (miniaudio) and 13 Pico-spinlock `spin_lock`
#      hits from beating 1968 RTOS matches (FreeRTOS) — both are real regressions.
#  (c) GENERIC IS NEVER PRIMARY WHEN A SPECIFIC PACK CLEARS THE FLOOR. `generic`
#      is the honest "it's just a C library" fallback. Its broad vocabulary
#      (ctx/init/free/struct/string idioms) wins raw count on any sizeable library
#      and buried the true domain (lwip Networking, mbedtls Crypto, leveldb
#      Databases). So when the count leader is `generic` but some SPECIFIC pack
#      has >= GENERIC_FLOOR (3) matches, the best-supported specific pack becomes
#      primary and generic drops to last. With no specific pack clearing the floor
#      (klib/uthash/sds containers), generic stays primary — the honest result.
# When nothing confident matches, detect() emits nothing -> unknown-domain.
# ---------------------------------------------------------------------------
detect() {
  local repo="$1"
  local id label rx anchor count files prio anchors
  local raw=""
  while IFS= read -r id; do
    [ -n "$id" ] || continue
    rx="$(pack_regex "$id")"
    # Materialize the anchor list once (avoids re-running rg, and avoids a
    # `head`-on-a-huge-`sort` SIGPIPE on big repos like dr_libs). The first line
    # is the anchor; the line count is the code-match count; the distinct path
    # count (`files`) is the match SPREAD used by the file-spread floor below.
    anchors="$(pack_anchors "$rx" "$repo" "$(pack_mode "$id")")"
    [ -n "$anchors" ] || continue
    count="$(printf '%s\n' "$anchors" | awk 'END { print NR }')"
    # Drop single incidental hits (count<=1): F2b. They are the __packed__ ->
    # Networking, "ideal%" -> Crypto, "coordinate system" class of false packs.
    [ "$count" -ge 2 ] || continue
    # Distinct-file spread: anchors are "path:line"; the path is everything up to
    # the LAST colon. A pack concentrated in one file (klib SIMD in ksw.c only,
    # sds packed-structs in sds.h only) must not out-promote a broadly-spread
    # domain/generic pack (the SIMD-density steal). Counted from the de-duplicated
    # anchor list so it is deterministic.
    files="$(printf '%s\n' "$anchors" | awk -F: '{ sub(/:[0-9]+$/, "", $0); print }' | LC_ALL=C sort -u | awk 'END { print NR }')"
    anchor="$(printf '%s\n' "$anchors" | awk 'NR==1 { print; exit }')"
    [ -n "$anchor" ] || continue
    label="$(pack_label "$id")"
    prio="$(pack_priority "$id")"
    raw="${raw}${prio}\t${count}\t${id}\t${label}\t${anchor}\t${files}"$'\n'
  done < <(pack_ids)

  [ -n "$raw" ] || return 0

  # Base order: COUNT desc, then original pack order asc (the priority column is
  # carried for the tier-1 floor logic but is NOT the primary sort key any more).
  printf '%b' "$raw" \
    | awk -F'\t' 'NF>=6 { print NR "\t" $0 }' \
    | LC_ALL=C sort -t$'\t' -k3,3nr -k1,1n \
    | awk -F'\t' '
        BEGIN { TIER1_FLOOR = 3; TIER1_RATIO = 10; GENERIC_FLOOR = 3; SPECIFIC_FILE_FLOOR = 2; GENERIC_DOMINANCE = 4 }
        { order[NR]=$1; prio[NR]=$2; count[NR]=$3
          id[NR]=$4; label[NR]=$5; anchor[NR]=$6; files[NR]=$7; n=NR }
        END {
          # Count leader (row 1 after the count-desc sort).
          leader = 1
          # (b) Tier-1 promotion: the highest-count tier-1 pack is promoted to
          # primary ONLY if it clears the floor and is within TIER1_RATIO of the
          # leader. Otherwise tier-1 gets no special standing (ranked by count).
          tier1 = 0
          for (i = 1; i <= n; i++) {
            if (prio[i] == "1") {
              if (count[i] >= TIER1_FLOOR && count[i] * TIER1_RATIO >= count[leader]) {
                tier1 = i
              }
              break   # rows are count-sorted; first tier-1 is the strongest
            }
          }
          primary = (tier1 > 0 ? tier1 : leader)
          # (c) Generic demotion: if the chosen primary is generic but a specific
          # pack clears GENERIC_FLOOR, the best-supported specific pack wins — but
          # only if that specific pack is (i) SPREAD across >= SPECIFIC_FILE_FLOOR
          # files AND (ii) DOMINANT enough that generic does not out-count it by more
          # than GENERIC_DOMINANCE (i.e. specific*DOMINANCE >= generic).
          #   (i)  A pack whose entire signal lives in ONE file is incidental density,
          #        not a domain (klib SIMD in ksw.c alone, sds packed-structs in sds.h
          #        alone) — it must not out-promote the honest Generic classification.
          #   (ii) A grab-bag library (klib) carries a small REAL networking module
          #        (knetfile/kopen: 21 socket/connect hits across 5 files) that clears
          #        both the count- and file-floors yet is dwarfed 12x by the 267
          #        container/string idioms — that minority domain is not the identity.
          #        The legitimate promotions (nng Net 497 vs generic 1593 = 3.2x,
          #        mbedtls Net 294 vs 780 = 2.6x, redis DB 2625 vs 4399 = 1.7x) all sit
          #        within 4x, so DOMINANCE=4 keeps them while dropping klib (21*4 < 267)
          #        back to the honest Generic primary.
          # When no specific pack clears ALL gates, generic stays primary.
          if (id[primary] == "generic") {
            best_specific = 0
            for (i = 1; i <= n; i++) {
              if (id[i] != "generic" && count[i] >= GENERIC_FLOOR && files[i] >= SPECIFIC_FILE_FLOOR && count[i] * GENERIC_DOMINANCE >= count[primary]) { best_specific = i; break }
            }
            if (best_specific > 0) primary = best_specific
          }
          # (d) iter43 protocol-rank file-spread flip: a PROTOCOL library (curl/libcoap)
          # spreads its networking signal across the whole transfer engine while concentrating
          # crypto in a TLS-backend subdir (curl lib/vtls + lib/vauth), so net touches MORE
          # files than crypto; a true crypto lib does the opposite (secp256k1 net 7f vs crypto
          # 17f, wolfssl 60 vs 367, libsodium 2 vs 236). When crypto is the chosen primary but
          # networking is spread across > NET_SPREAD x its files, flip primary to networking.
          # Promotion-only and crypto->networking-only. K=1.3 sits mid-window in the empirically
          # safe range [0.92, 1.48): libcoap (net 34f / crypto 23f = 1.48) flips, libwebsockets
          # (151/165 = 0.92) correctly does NOT. mbedtls is already Networking-primary by count
          # (so untouched). Full 160-repo sweep: exactly curl + libcoap flip, all else identical.
          NET_SPREAD = 1.3
          if (id[primary] == "crypto") {
            ci = primary; ni = 0
            for (i = 1; i <= n; i++) if (id[i] == "networking") { ni = i; break }
            if (ni > 0 && files[ni] > files[ci] * NET_SPREAD) primary = ni
          }
          # Emit: primary first, then the rest in count order; generic always
          # last (it is the least-specific label whenever a real pack exists).
          emitted = 0
          emit(++emitted, primary)
          for (i = 1; i <= n; i++) if (i != primary && id[i] != "generic") emit(++emitted, i)
          for (i = 1; i <= n; i++) if (i != primary && id[i] == "generic") emit(++emitted, i)
        }
        function emit(rank, i,   role) {
          role = (rank == 1 ? "primary" : "secondary")
          printf "%s\t%s\t%s\t%s\t%s\n", id[i], label[i], anchor[i], count[i], role
        }'
}

emit_text() {
  # tab-separated id/label/anchor/count/role -> "pack[role]: <label> | <anchor>
  # (<count> code matches)", or the unknown-domain line when there were no
  # confident matches.
  local rows="$1"
  if [ -z "$rows" ]; then
    printf 'unknown-domain: build a pack from references/UNKNOWN-DOMAIN.md\n'
    return 0
  fi
  printf '%s\n' "$rows" \
    | awk -F'\t' 'NF>=5 {
        printf "pack[%s]: %s | %s (%s code matches)\n", $5, $2, $3, $4 }'
}

emit_json() {
  # tab-separated id/label/anchor/count/role -> {"matched": bool, "packs": [...]}.
  local rows="$1"
  printf '%s\n' "$rows" | awk -F'\t' '
    function esc(s,   r) {
      r = s
      gsub(/\\/, "\\\\", r)
      gsub(/"/, "\\\"", r)
      return r
    }
    BEGIN { n = 0 }
    NF>=5 {
      n++
      id[n] = $1; label[n] = $2; anchor[n] = $3; count[n] = $4; role[n] = $5
    }
    END {
      printf "{\n"
      printf "  \"matched\": %s,\n", (n > 0 ? "true" : "false")
      printf "  \"packs\": ["
      for (i = 1; i <= n; i++) {
        if (i > 1) printf ","
        printf "\n    {\"id\": \"%s\", \"pack\": \"%s\", \"anchor\": \"%s\", \"matches\": %s, \"role\": \"%s\"}", \
          esc(id[i]), esc(label[i]), esc(anchor[i]), (count[i]+0), esc(role[i])
      }
      if (n > 0) printf "\n  "
      printf "]"
      if (n == 0) {
        printf ",\n  \"hint\": \"build a pack from references/UNKNOWN-DOMAIN.md\""
      }
      printf "\n}\n"
    }'
}

run_detect() {
  local repo="$1"
  local json="$2"
  if [ ! -d "$repo" ]; then
    printf 'error: not a directory: %s\n' "$repo" >&2
    exit 2
  fi
  if ! rg_available; then
    printf 'error: rg (ripgrep) is required for cpp_domain_detect.sh\n' >&2
    exit 3
  fi
  # G9 (150-repo gauntlet): for a large/amalgam repo, pre-strip a bounded sample
  # ONCE so the 14 per-pack scans re-match a cached corpus instead of re-listing +
  # re-stripping the whole tree each time (which timed out — exit 124 — on nuttx,
  # mongoose, betaflight, wolfssl). Normal repos skip this entirely and keep their
  # exact calibrated per-pack path.
  local _corpus_dir=""
  local srcn srcb _flist
  _flist="$( { rg --files --no-messages \
      --glob '*.{c,cc,cpp,cxx,h,hh,hpp,hxx,cu,cuh,cl}' --glob '!**/.git/**' \
      "${EXCLUDE_GLOBS[@]}" -- "$repo" 2>/dev/null || true; }; )"
  srcn="$(printf '%s\n' "$_flist" | awk 'NF' | wc -l | tr -d ' ')"
  # Trigger on file COUNT or total BYTES: a single-file amalgam (mongoose's 1 MB
  # mongoose.c) is few files but char-walking it once per pack still times out, so a
  # byte budget catches what the file-count cap misses. Byte sum via stat (no reads);
  # only computed when the file-count gate did not already fire.
  srcb=0
  if [ "${srcn:-0}" -le "${DOMAIN_FILE_CAP}" ]; then
    srcb="$(printf '%s\n' "$_flist" | awk 'NF' | tr '\n' '\0' \
        | xargs -0 -r stat -c '%s' 2>/dev/null | awk '{s+=$1} END{print s+0}')"
  fi
  if [ "${srcn:-0}" -gt "${DOMAIN_FILE_CAP}" ] || [ "${srcb:-0}" -gt "${DOMAIN_BYTE_CAP:-6000000}" ]; then
    _corpus_dir="$(mktemp -d 2>/dev/null || true)"
    if [ -n "$_corpus_dir" ]; then
      # ONE corpus over the 'all' globs (superset of 'code'); both modes re-match it.
      DOMAIN_CORPUS_ALL="$_corpus_dir/all"
      build_domain_corpus "$repo" all "$DOMAIN_CORPUS_ALL"
      DOMAIN_CORPUS_CODE="$DOMAIN_CORPUS_ALL"
      printf '[scope] large repo (%s source files / %s bytes): domain scored from a bounded, evenly-spread sample (~%s files, first %s lines each) to stay in budget — re-run on a touched subdirectory for an exhaustive scan.\n' \
        "$srcn" "${srcb:-?}" "${DOMAIN_SAMPLE}" "${DOMAIN_LINECAP}" >&2
    fi
  fi
  local rows
  rows="$(detect "$repo")"
  [ -n "$_corpus_dir" ] && rm -rf "$_corpus_dir" 2>/dev/null
  DOMAIN_CORPUS_ALL=""
  DOMAIN_CORPUS_CODE=""
  if [ "$json" = yes ]; then
    emit_json "$rows"
  else
    emit_text "$rows"
  fi
}

# ---------------------------------------------------------------------------
# Self-test: build tiny fake repos, prove the documented signal->pack mappings
# fire, prove a plain repo reports unknown-domain, prove reproducibility and
# no absolute-path leak. Cleans up via trap.
# ---------------------------------------------------------------------------
self_test() {
  if ! rg_available; then
    printf 'cpp_domain_detect self-test: FAIL (rg not available)\n'
    exit 1
  fi
  local tmp
  tmp="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp'" EXIT

  # Fixture A: a CUDA file -> GPU pack.
  mkdir -p "$tmp/gpu/src"
  cat >"$tmp/gpu/src/kernel.cu" <<'SRC'
__global__ void add(const float *a, const float *b, float *c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) c[i] = a[i] + b[i];
}
void launch(void) { cudaMalloc((void**)0, 0); }
SRC

  # Fixture B: a __user + MODULE_LICENSE file -> kernel pack.
  mkdir -p "$tmp/kernel/drivers"
  cat >"$tmp/kernel/drivers/foo.c" <<'SRC'
#include <linux/module.h>
long do_ioctl(void __user *arg) {
    char buf[16];
    if (copy_from_user(buf, arg, sizeof(buf)))
        return -1;
    return 0;
}
MODULE_LICENSE("GPL");
SRC

  # Fixture C: a plain repo -> unknown-domain.
  mkdir -p "$tmp/plain/src"
  cat >"$tmp/plain/src/main.c" <<'SRC'
#include <stdio.h>
int main(void) {
    printf("hello\n");
    return 0;
}
SRC

  # Fixture D (F2c regression): a SIMD/HPC repo whose ONLY build flag is
  # `-ffast-math` plus `_mm_*` intrinsics. The HPC pack regex begins with a dash;
  # before the `-e` fix rg parsed `-f` as `--file` and the pack silently vanished
  # (cglm-class miss). This fixture must select HPC.
  mkdir -p "$tmp/hpc"
  cat >"$tmp/hpc/CMakeLists.txt" <<'SRC'
add_compile_options(-ffast-math -march=native)
SRC
  cat >"$tmp/hpc/simd.c" <<'SRC'
#include <immintrin.h>
void axpy(float *y, const float *x, float a, int n) {
    __m128 va = _mm_set1_ps(a);
    for (int i = 0; i < n; i += 4) {
        __m128 vx = _mm_loadu_ps(x + i);
        __m128 vy = _mm_loadu_ps(y + i);
        _mm_storeu_ps(y + i, _mm_add_ps(vy, _mm_mul_ps(va, vx)));
    }
}
SRC

  # Fixture P (F2a parser): a JSON parser. Named format + *_parse API -> Parser.
  mkdir -p "$tmp/parser"
  cat >"$tmp/parser/jsonparse.c" <<'SRC'
#include "jsonparse.h"
int json_parse(const char *buf, size_t len, struct json_token *out) {
    if (len == 0) return -1;          /* validate length before read */
    return parse_value(buf, len, out);
}
int parse_value(const char *buf, size_t len, struct json_token *out) {
    return tokenize_json(buf, len, out);
}
SRC

  # Fixture G (F2a generic + F2b ranking): a header-only container library that
  # ALSO carries exactly one incidental `__attribute__((__packed__))` (the
  # klib/sds misfire). It must select Generic, and the single packed-struct hit
  # must NOT promote a confident "Networking" pack.
  mkdir -p "$tmp/generic"
  cat >"$tmp/generic/vec.h" <<'SRC'
#ifndef VEC_H
#define VEC_H
typedef struct vec_s { int *data; size_t len, cap; } vec_t;
struct __attribute__((__packed__)) vec_hdr { unsigned char tag; };
static inline vec_t *vec_init(void) { return calloc(1, sizeof(vec_t)); }
static inline void vec_free(vec_t *v) { free(v->data); free(v); }
static inline void vec_destroy(vec_t *v) { vec_free(v); }
#endif
SRC

  # Fixture K (F2b comment-stripping): domain tokens appear ONLY in comments.
  # They must be stripped, leaving no confident pack -> unknown-domain.
  mkdir -p "$tmp/comments"
  cat >"$tmp/comments/util.c" <<'SRC'
/* This converts a left-handed coordinate system to right-handed. */
/* It does not use crypto_box or any EVP_ routine; see the ring buffer note. */
// htons() is mentioned here only in prose, not called.
int util_clamp(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
SRC

  # Fixture Z (R8 case-fold SPACE FP): a zlib-like compression lib whose ONLY
  # "SPACE" tokens are the gzip RFC-1952 header byte `OS_CODE` (uppercase) and a
  # lowercase `os_flush()` C++ method. Case-INSENSITIVE matching let `\bOS_[A-Z]`
  # match both → SPACE won PRIMARY on a compression lib. With the case-sensitive
  # `OS_(?!CODE\b)` token, NEITHER matches, so the repo must NOT be SPACE-primary.
  mkdir -p "$tmp/zlibish"
  cat >"$tmp/zlibish/zutil.h" <<'SRC'
#ifndef ZUTIL_H
#define ZUTIL_H
#  define OS_CODE  0x03   /* gzip OS identifier byte (RFC 1952) */
int inflate(void *strm, int flush);
int deflate(void *strm, int flush);
int compress(unsigned char *dest, unsigned long *destLen);
#endif
SRC
  cat >"$tmp/zlibish/zstream.cc" <<'SRC'
class ozstream {
  void os_flush() { /* lowercase OS_ should not be a SPACE token */ }
  unsigned char *buf_init(void) { return 0; }
};
SRC

  # Fixture C (R8 real SPACE survives): a cFS-like flight-software repo using the
  # genuine NASA uppercase API `CFE_*` and `OS_*` (NOT OS_CODE). It MUST stay
  # SPACE-primary under case-sensitive matching.
  mkdir -p "$tmp/cfsish/fsw"
  cat >"$tmp/cfsish/fsw/cfe_es.c" <<'SRC'
#include "cfe_es.h"
int32 CFE_ES_RegisterApp(void) {
    OS_TaskCreate(0, "APP", 0, 0, 0, 0);
    OS_printf("registered\n");
    return CFE_ES_RunLoop(0);
}
int32 CFE_EVS_SendEvent(uint16 id) { return OS_MAX_API_LEN + id; }
SRC

  # Fixture F (R8 opcode + R3+ CamelCase test dir): an F´-like repo. The Compilers
  # signal must come ONLY from a CamelCase `FppTestProject/` test dir (excluded by
  # R3+) plus the CamelCase TYPE `FwOpcodeType` (which case-sensitive `\bopcode\b`
  # must NOT match). The shipped code carries genuine SPACE `OS_*`/`CFE_*` tokens.
  # Net: SPACE must win PRIMARY; the `FwOpcodeType`/test-dir Compilers signal must
  # NOT steal it (was: Compilers-primary off `\bopcode\b` matching `FwOpcodeType`).
  mkdir -p "$tmp/fprimeish/Svc/Cmd" "$tmp/fprimeish/FppTestProject/FppTest"
  cat >"$tmp/fprimeish/Svc/Cmd/CmdDispatcher.cpp" <<'SRC'
#include "CmdDispatcher.hpp"
void CmdDispatcher::dispatch(FwOpcodeType opCode) {
    OS_TaskDelay(1);
    CFE_SB_TransmitMsg(opCode);
}
SRC
  cat >"$tmp/fprimeish/FppTestProject/FppTest/Driver.cpp" <<'SRC'
/* Test-only driver: opcode opcode opcode bytecode interpreter codegen llvm:: */
void run_opcode_interpreter() { /* opcode bytecode codegen interpreter */ }
SRC

  # Fixture COMP (R9-vocab): a zlib/lz4-like compression codec. Its defining
  # vocabulary (`deflate`/`inflate`/`compress`/`crc32`/`LZ4_`/`huffman`) must select
  # the NEW Compression pack as PRIMARY — before R9-vocab there was no compression
  # pack, so zlib mis-primaried Networking and lz4 mis-primaried Parser. The file
  # also carries the codec identifier `decode_full_block`: the NARROWED Parser
  # `_decode` token (now format/parser-prefixed) must NOT match it, so Parser does
  # not steal a codec.
  mkdir -p "$tmp/codec/lib"
  cat >"$tmp/codec/lib/deflate.c" <<'SRC'
#include "deflate.h"
typedef enum { decode_full_block = 0, partial_decode = 1 } earlyEnd_directive;
int deflate(void *strm, int flush) { return huffman_encode(strm); }
int inflate(void *strm, int flush) { return LZ4_decompress_safe(strm, flush); }
int inflateBack(void *strm) { return 0; }
int compress(unsigned char *dest, unsigned long *destLen) { return 0; }
int uncompress(unsigned char *dest, unsigned long *srcLen) { return 0; }
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned len) { return crc; }
unsigned long adler32(unsigned long adler, const unsigned char *buf, unsigned len) { return adler; }
SRC
  cat >"$tmp/codec/lib/lz4.h" <<'SRC'
#ifndef LZ4_H
#define LZ4_H
int LZ4_compress_default(const char *src, char *dst, int srcSize, int dstCap);
int LZ4_decompress_safe(const char *src, char *dst, int compressedSize, int dstCap);
#endif
SRC

  # Fixture NET (R9-vocab): a messaging/transport library using the socket/listener/
  # dialer/send/recv idiom — the vocabulary nng is built on. Before R9-vocab the
  # Networking pack only knew ntohl/htons/packed-struct/RFC and scored ~9 on nng,
  # losing PRIMARY to Parser. It must now select Networking PRIMARY.
  mkdir -p "$tmp/net/src"
  cat >"$tmp/net/src/transport.c" <<'SRC'
#include "transport.h"
int sock_open(void) {
    int s = socket(2, 1, 0);
    bind(s, 0, 0);
    listen(s, 5);
    int c = accept(s, 0, 0);
    connect(c, 0, 0);
    send(c, "x", 1, 0);
    recv(c, 0, 0, 0);
    return c;
}
SRC
  cat >"$tmp/net/src/dialer.c" <<'SRC'
#include "dialer.h"
struct listener { int fd; };
struct dialer { int fd; };
void endpoint_init(struct listener *l, struct dialer *d) { (void)l; (void)d; }
SRC

  # Fixture CRY (R9-vocab): a hash/cipher library (blake2-like) whose identity is the
  # primitive names `blake2`/`sha256`/`hmac`/`digest`/`chacha`/`poly1305`/`nonce`,
  # NOT the constant-time/EVP_/FIPS engineering signals. Before R9-vocab a textbook
  # hash lib scored ~10 Crypto (incidental `test.vector`) and lost PRIMARY to HPC off
  # its own SIMD intrinsics. It must now select Crypto PRIMARY.
  mkdir -p "$tmp/cipher"
  cat >"$tmp/cipher/blake2b.c" <<'SRC'
#include "blake2.h"
int blake2b_update(void *S, const void *in, unsigned long inlen) { return 0; }
int blake2b_final(void *S, void *out, unsigned long outlen) { return 0; }
void chacha20_block(unsigned char *keystream, const unsigned char *nonce) { (void)nonce; }
int poly1305_mac(unsigned char *digest, const unsigned char *m) { return 0; }
int hmac_sha256(unsigned char *out, const unsigned char *key) { return 0; }
SRC

  # Fixture FP2 (R9-vocab): an F´/F-Prime flight repo whose vocabulary is fprime's
  # own — `CCSDS`/`Framer`/`Deframer`/`Tlm`/`APID`/`FwOpcode`/`CmdResponse`/`telemetry`
  # — NOT cFE's `OS_*`/`CFE_*`. Before R9-vocab the Space pack was cFE-tuned and blind
  # to F´, so fprime barely cleared Space on incidental `OS_[A-Z]` collisions. It must
  # select Space PRIMARY off the F´ vocabulary alone (no OS_/CFE_ tokens here).
  mkdir -p "$tmp/fprime2/Svc/ComFramer"
  cat >"$tmp/fprime2/Svc/ComFramer/Framer.cpp" <<'SRC'
#include "Framer.hpp"
void Framer::frame(FwOpcodeType opCode) {
    CCSDS_Header hdr;
    hdr.APID = 0x10;
    Deframer deframer;
    TlmChan telemetry;
    CmdResponse resp;
    (void)opCode; (void)hdr; (void)deframer; (void)telemetry; (void)resp;
}
SRC

  # Fixture DB (R12): a storage engine whose identity is SQL/btree/pager/WAL/vdbe/
  # rowid/PRAGMA + redis RDB/AOF/keyspace + LSM compaction/memtable/manifest. Before
  # R12 the Databases pack knew only fsync/WAL/MVCC/pwrite, so sqlite mis-primaried
  # Parser and redis Networking (DB ranked last). It must now select Databases PRIMARY.
  # It ALSO carries one Parser-ish `json_parse` and one Net `socket` so the win is by
  # the DB vocabulary mass, not by being the only pack present.
  mkdir -p "$tmp/db/src"
  cat >"$tmp/db/src/btree.c" <<'SRC'
#include "btree.h"
int sqlite3BtreeOpen(Pager *pPager, int rowid) {
    /* btree pager + WAL + vdbe + rowid + write-ahead log */
    return pPager->pageCache;
}
int sqlite3VdbeExec(void *p) { return 0; }       /* vdbe opcode dispatch */
int rdbSaveRio(void *rdb) { return 0; }           /* redis RDB snapshot */
int aofRewrite(void *aof) { return 0; }           /* redis AOF write-ahead */
int compactMemtable(void *memtable) { return 0; } /* LSM compaction + memtable */
int loadManifest(void *manifest) { return 0; }    /* LSM manifest + sstable */
int doVacuum(void *db) { return 0; }              /* VACUUM */
int beginTransaction(void *db) { return 0; }      /* transaction + commit + rollback */
int json_parse(const char *s) { return 0; }       /* one Parser token (must lose) */
int sock_open(void) { return socket(2, 1, 0); }   /* one Net token (must lose) */
SRC
  cat >"$tmp/db/src/pragma.c" <<'SRC'
#include "pragma.h"
/* SQL PRAGMA handler. PRAGMA is the SQL keyword (case-sensitive), NOT C #pragma. */
int sqlite3Pragma(void *p) { return 0; }
int keyspaceNotify(void *db) { return 0; }   /* redis keyspace */
SRC

  # Fixture DBPRAGMA (R12 over-correction guard): a C++ header that uses `#pragma
  # once`/`#pragma clang` (the C PREPROCESSOR directive) MANY times but has ZERO real
  # DB code. The bare-`pragma` token had matched every `#pragma`, handing a test
  # framework (Catch2) a false Databases primary off 228 `#pragma`s. The case-sensitive
  # `(?-i:PRAGMA|Pragma)` token must NOT match lowercase `#pragma`, so this repo must
  # NOT be Databases-primary (it is a generic parser-ish lib instead).
  mkdir -p "$tmp/pragmaonly"
  cat >"$tmp/pragmaonly/widget.hpp" <<'SRC'
#pragma once
#pragma clang diagnostic push
#pragma GCC diagnostic ignored "-Wfoo"
#pragma once
class Widget { public: int json_decode(const char *s); };
int xml_parse(const char *s);
int parse_token(const char *s);
SRC

  # Fixture DATA (R13): a JS-engine-like Compilers repo (opcode/bytecode/interpreter/
  # codegen + lexer) that ALSO ships a Unicode DATA TABLE `UnicodeData.txt` full of
  # "CYRILLIC LETTER SHA" rows and a `Makefile` with a `# SHA1: …` checksum comment.
  # Before R13 the `\bsha\b` Crypto token matched the data table + Makefile comment and
  # Crypto stole PRIMARY (the duktape failure). With the data-file skip + `#`-comment
  # strip in non-C files, this must select Compilers/interpreters/VMs, NOT Crypto.
  mkdir -p "$tmp/jsvm/src-input"
  cat >"$tmp/jsvm/src-input/compiler.c" <<'SRC'
#include "compiler.h"
int interpreter_dispatch(int opcode, void *bytecode) {
    return codegen(opcode, bytecode);
}
int codegen(int opcode, void *bytecode) { return emitOpcode(opcode); }
int interpreter_step(void *vm) { return 0; }
int bytecode_verify(void *vm) { return 0; }
SRC
  cat >"$tmp/jsvm/src-input/UnicodeData.txt" <<'SRC'
0428;CYRILLIC CAPITAL LETTER SHA;Lu;0;L;;;;;N;;;;0448;
0429;CYRILLIC CAPITAL LETTER SHCHA;Lu;0;L;;;;;N;;;;0449;
0531;ARMENIAN CAPITAL LETTER SHA;Lu;0;L;;;;;N;;;;0561;
SRC
  cat >"$tmp/jsvm/Makefile" <<'MK'
# SHA1: 774be8b65b9b3d2b8b8d md5 digest sha256 hmac nonce blake2 chacha
all:
	$(CC) -c compiler.c
MK

  # Fixture CATCH (R11): a vendored TEST FRAMEWORK that IS the audited repo — its own
  # source lives under `src/catch2/` (the basename the old `!**/catch2/**` glob excluded
  # everywhere). The anchored R11 glob excludes a vendored catch2 only under a vendored
  # parent, so the framework's OWN `src/catch2/` MUST now be scanned (a real risk/code
  # signal must appear from it). A SEPARATE embedded copy under `tests/vendor/catch2/`
  # carries a DECOY token that must STILL be excluded.
  mkdir -p "$tmp/catchfw/src/catch2" "$tmp/catchfw/tests/vendor/catch2"
  cat >"$tmp/catchfw/src/catch2/catch_tostring.cpp" <<'SRC'
#include "catch_tostring.hpp"
/* the framework's own shipped source must be scannable */
int xml_parse(const char *s) { return parse_token(s); }
int parse_token(const char *s) { return 0; }
int json_decode(const char *s) { return 0; }
SRC
  cat >"$tmp/catchfw/tests/vendor/catch2/embedded.cpp" <<'SRC'
/* an EMBEDDED vendored copy under tests/vendor/ — must STILL be excluded */
__global__ void decoy_kernel(void) { cudaMalloc(0, 0); }
SRC

  # Fixture DEPS (R10): a pure-C parser repo whose OWN `src/` is a small JSON parser,
  # but which bundles a large vendored library under the bare `deps/` dir (the redis
  # convention) plus a `singleheader/` amalgam and an `autosetup/jimsh0.c` bootstrap
  # interpreter. Before R10 those vendored trees outvoted the real `src/` signal. The
  # repo's primary must come from `src/` (Parser), and NO anchor may point into deps/,
  # singleheader/, or autosetup/jimsh0.c.
  mkdir -p "$tmp/depsrepo/src" "$tmp/depsrepo/deps/jimtcl" \
           "$tmp/depsrepo/singleheader" "$tmp/depsrepo/autosetup"
  cat >"$tmp/depsrepo/src/json.c" <<'SRC'
#include "json.h"
int json_parse(const char *buf, int len) { return parse_value(buf, len); }
int parse_value(const char *buf, int len) { return tokenize_json(buf, len); }
SRC
  # Vendored bundle: a huge interpreter amalgam that would mis-primary Compilers/VMs.
  cat >"$tmp/depsrepo/deps/jimtcl/jim.c" <<'SRC'
int jim_opcode_interpreter_bytecode_codegen(void) { return 0; }
int jim_opcode_two(void) { return 0; }
int jim_interpreter_three(void) { return 0; }
SRC
  cat >"$tmp/depsrepo/singleheader/amalgam.h" <<'SRC'
int opcode_bytecode_interpreter_codegen_amalgam(void) { return 0; }
SRC
  cat >"$tmp/depsrepo/autosetup/jimsh0.c" <<'SRC'
/* bootstrap version of Jim Tcl */
int jimsh_opcode_bytecode_interpreter(void) { return 0; }
SRC

  # Fixture PNG (P4 codec-WRAPPER vocab + SIMD-density tie-break): an image codec
  # shaped like libpng. Its codec identity is the chunk/wrapper vocabulary
  # (IDAT/IHDR/IEND/PLTE/zstream/png_inflate/unfilter/scanline/palette) spread across
  # MANY files, but it ALSO ships ONE dense ARM-NEON filter file full of `vld1` HPC
  # intrinsics. Before P4 the Compression pack lacked the wrapper vocab, so the dense
  # 1-file NEON intrinsics won HPC primary off a real codec (libpng HPC 75 vs Compression
  # 18). It must now select Compression PRIMARY; the dense NEON file must NOT steal it.
  mkdir -p "$tmp/pngish/arm"
  cat >"$tmp/pngish/pngread.c" <<'SRC'
#include "png.h"
int png_read_IDAT_data(void *png_ptr) { return png_inflate(png_ptr); }
int png_handle_IHDR(void *png_ptr) { return 0; }
int png_handle_IEND(void *png_ptr) { return 0; }
int png_handle_PLTE(void *png_ptr) { return 0; }
void png_read_filter_row(void *rp) { (void)rp; }
SRC
  cat >"$tmp/pngish/pngwutil.c" <<'SRC'
#include "png.h"
int png_write_IDAT(void *png_ptr) { return png_deflate(png_ptr); }
int png_do_unfilter(void *row) { return 0; }
int png_build_scanline(void *png_ptr) { return 0; }
int png_set_palette(void *png_ptr) { return 0; }
SRC
  cat >"$tmp/pngish/pngrutil.c" <<'SRC'
#include "png.h"
struct zstream { int x; };
int png_inflate(void *png_ptr) { return 0; }
int png_deflate(void *png_ptr) { return 0; }
int png_read_scanline(void *png_ptr) { return 0; }
SRC
  cat >"$tmp/pngish/arm/filter_neon_intrinsics.c" <<'SRC'
#include <arm_neon.h>
void png_read_filter_row_up_neon(unsigned char *rp, const unsigned char *pp) {
    uint8x16_t a = vld1q_u8(rp);
    uint8x16_t b = vld1q_u8(pp);
    uint8x16_t c = vld1q_u8(rp + 16);
    uint8x16_t d = vld1q_u8(pp + 16);
    uint8x16_t e = vld1q_u8(rp + 32);
    vst1q_u8(rp, a); vst1q_u8(rp + 16, b); vst1q_u8(rp + 32, c);
    (void)d; (void)e;
}
SRC

  # Fixture RJSON (P4 CamelCase parser vocab + R1± header-only C++): a header-only
  # C++ JSON serializer shaped like rapidjson. Its parse API is CamelCase
  # (`ParseValue`/`ParseString`/`SkipWhitespace`/`kParseErrorNone`) spread across
  # headers, but it ALSO carries SSE2/NEON scan intrinsics in two headers. Before P4
  # the Parser pack only knew snake_case `*_parse`, so rapidjson scored 11 Parser and
  # mis-primaried HPC off its 2 SIMD scan headers. It must now select Parser PRIMARY.
  mkdir -p "$tmp/rjsonish/include/rj"
  cat >"$tmp/rjsonish/include/rj/reader.h" <<'SRC'
namespace rj {
template <typename T> struct GenericReader {
    void SkipWhitespace(T &is) { (void)is; }
    int ParseValue(T &is) { return ParseString(is); }
    int ParseString(T &is) { (void)is; return kParseErrorNone; }
    int ParseNumber(T &is) { (void)is; return 0; }
    /* SSE2 fast scan */
    void Scan() { __m128 v = _mm_set1_ps(0.0f); (void)v; }
};
enum { kParseErrorNone = 0, kParseErrorValueInvalid = 1 };
}
SRC
  cat >"$tmp/rjsonish/include/rj/writer.h" <<'SRC'
namespace rj {
template <typename T> struct GenericWriter {
    int ParseObject(T &is) { (void)is; return 0; }
    void Scan() { __m128 v = _mm_set1_ps(1.0f); (void)v; }
};
}
SRC
  cat >"$tmp/rjsonish/include/rj/document.h" <<'SRC'
namespace rj {
template <typename T> struct GenericDocument {
    int ParseStream(T &is) { (void)is; return 0; }
    int ParseInsitu(T &is) { (void)is; return kParseDefaultFlags; }
};
enum { kParseDefaultFlags = 0 };
}
SRC

  # Fixture ASSET (iter43 asset-loader): a cgltf-shaped 3D model importer. Its identity
  # is the cgltf_* binary-buffer accessor API plus a bounds-checked bufferView indexing
  # line — NOT generic text parsing. It must select the asset-loader pack PRIMARY, NOT
  # Parser, proving the new pack out-ranks Parser on a real model loader.
  mkdir -p "$tmp/assetish/src"
  cat >"$tmp/assetish/src/loader.c" <<'SRC'
#include "cgltf.h"
cgltf_result load_model(const char *path, cgltf_data **out) {
    cgltf_options opts = {0};
    cgltf_data *data = NULL;
    cgltf_result r = cgltf_parse(&opts, path, 0, &data);
    if (r != cgltf_result_success) return r;
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        cgltf_mesh *mesh = &data->meshes[i];
        for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
            cgltf_primitive *prim = &mesh->primitives[p];
            cgltf_accessor *acc = prim->indices;
            cgltf_buffer_view *view = acc->buffer_view;
            cgltf_size req_size = acc->offset + acc->stride * (acc->count - 1);
            if (view->size < req_size) return cgltf_result_data_too_short;
            char *ptr = (char*)view->buffer->data + acc->offset + view->offset;
            (void)ptr;
        }
    }
    *out = data;
    return cgltf_result_success;
}
SRC
  cat >"$tmp/assetish/src/scene.c" <<'SRC'
#include "cgltf.h"
void walk_scene(cgltf_data *data) {
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        cgltf_node *node = &data->nodes[i];
        cgltf_skin *skin = node->skin;
        (void)skin;
    }
    for (cgltf_size a = 0; a < data->animations_count; ++a) {
        cgltf_animation *anim = &data->animations[a];
        (void)anim;
    }
}
SRC

  # Fixture GRABBAG (P4 file-spread + dominance floors): a grab-bag C library shaped
  # like klib — its identity is many container/string idioms (`*_init`/`*_free`/
  # `typedef struct {`/`KHASH`) across MANY files (the Generic count leader), PLUS a
  # SMALL real networking module (`socket`/`connect`/`setsockopt` across 2 files) AND
  # ONE dense SIMD file. Before P4 the dense SIMD won HPC (file floor fixes that), and
  # then the minority networking module won Networking via generic-demotion even though
  # generic out-counts it >10x. With BOTH the file-spread floor and the 4x dominance
  # floor it must settle on Generic — the honest "it is just a C library" answer.
  mkdir -p "$tmp/grabbag"
  # Many container/string idioms (the Generic count leader). The grab-bag must carry
  # FAR more generic idioms than the minority networking module (target >4x) so the
  # dominance floor keeps it Generic — exactly the klib shape (267 generic vs 21 net).
  cat >"$tmp/grabbag/khash.h" <<'SRC'
#define KHASH_MAP_INIT_INT(name, khval_t)
typedef struct kh_s { int n_buckets; int *keys; } khash_t;
static inline khash_t *kh_init_int(void) { return 0; }
static inline void kh_destroy_int(khash_t *h) { (void)h; }
static inline khash_t *kh_new_int(void) { return 0; }
typedef struct khm_s { int n; } khashmap_t;
static inline khashmap_t *khm_init(void) { return 0; }
static inline void khm_free(khashmap_t *h) { (void)h; }
static inline void khm_destroy(khashmap_t *h) { (void)h; }
SRC
  cat >"$tmp/grabbag/kvec.h" <<'SRC'
typedef struct kvec_s { int n, m; int *a; } kvec_int_t;
static inline kvec_int_t *kvec_init(void) { return 0; }
static inline void kvec_free(kvec_int_t *v) { (void)v; }
static inline void kvec_destroy(kvec_int_t *v) { (void)v; }
static inline kvec_int_t *kvec_new(void) { return 0; }
typedef struct kbuf_s { int n; } kbuf_t;
static inline kbuf_t *kbuf_init(void) { return 0; }
static inline void kbuf_free(kbuf_t *b) { (void)b; }
static inline void kbuf_destroy(kbuf_t *b) { (void)b; }
SRC
  cat >"$tmp/grabbag/kstring.h" <<'SRC'
typedef struct kstr_s { int l, m; char *s; } kstring_t;
static inline kstring_t *kstr_init(void) { return 0; }
static inline void kstr_free(kstring_t *s) { (void)s; }
static inline kstring_t *kstr_new(void) { return 0; }
static inline void kstr_destroy(kstring_t *s) { (void)s; }
typedef struct kpool_s { int n; } kpool_t;
static inline kpool_t *kpool_init(void) { return 0; }
static inline void kpool_free(kpool_t *p) { (void)p; }
static inline void kpool_destroy(kpool_t *p) { (void)p; }
SRC
  cat >"$tmp/grabbag/klist.h" <<'SRC'
typedef struct klist_s { void *head; } klist_t;
static inline klist_t *klist_init(void) { return 0; }
static inline void klist_free(klist_t *l) { (void)l; }
static inline void klist_destroy(klist_t *l) { (void)l; }
static inline klist_t *klist_new(void) { return 0; }
typedef struct kdeque_s { void *head; } kdeque_t;
static inline kdeque_t *kdeque_init(void) { return 0; }
static inline void kdeque_free(kdeque_t *d) { (void)d; }
static inline void kdeque_destroy(kdeque_t *d) { (void)d; }
SRC
  cat >"$tmp/grabbag/knetfile.c" <<'SRC'
#include "knetfile.h"
int knet_connect(int port) {
    int s = socket(2, 1, 0);
    connect(s, 0, 0);
    setsockopt(s, 0, 0, 0, 0);
    recv(s, 0, 0, 0);
    return s;
}
SRC
  cat >"$tmp/grabbag/kopen.c" <<'SRC'
#include "knetfile.h"
int knet_open(void) {
    int s = socket(2, 1, 0);
    connect(s, 0, 0);
    setsockopt(s, 0, 0, 0, 0);
    return s;
}
SRC
  cat >"$tmp/grabbag/ksw.c" <<'SRC'
#include <emmintrin.h>
int ksw_align(void) {
    __m128i a = _mm_set1_epi8(0);
    __m128i b = _mm_add_epi8(a, a);
    __m128i c = _mm_max_epu8(a, b);
    __m128i d = _mm_subs_epu8(c, a);
    (void)d; return 0;
}
SRC

  # Fixture LUAISH (P4 Filesystems barrier narrowing + Compilers vocab): a stack VM
  # shaped like lua. Its real domain is interpreter/VM (`opcode`/`bytecode`/
  # `interpreter`/`luaV_`/`luaK_`/`Proto`/`Instruction`) but it ALSO calls a GC write
  # barrier `luaC_barrier`/`luaC_barrierback` everywhere. Before P4 the bare
  # Filesystems `barrier` token matched all 44 `luaC_barrier` calls → lua mis-primaried
  # Filesystems. With `\bbarrier\b` (no boundary before `barrier` in `luaC_barrier`) the
  # GC calls no longer match, and the VM vocabulary selects Compilers PRIMARY.
  mkdir -p "$tmp/luaish"
  cat >"$tmp/luaish/lvm.c" <<'SRC'
#include "lvm.h"
int luaV_execute(void *L) {
    luaC_barrier(L, 0, 0);
    luaC_barrierback(L, 0, 0);
    return 0;   /* opcode bytecode interpreter dispatch */
}
int interpreter_step(void *L) { luaC_objbarrier(L, 0, 0); return 0; }
SRC
  cat >"$tmp/luaish/lcode.c" <<'SRC'
#include "lcode.h"
int luaK_code(void *fs, int opcode) { return opcode; }   /* codegen */
int luaK_codeABC(void *fs) { luaC_barrier(fs, 0, 0); return 0; }
SRC
  cat >"$tmp/luaish/lopcodes.h" <<'SRC'
#ifndef LOPCODES_H
#define LOPCODES_H
typedef struct Proto { int sizecode; } Proto;
typedef unsigned int Instruction;
/* opcode table for the bytecode interpreter */
enum OpCode { OP_MOVE, OP_LOADK };
#endif
SRC
  cat >"$tmp/luaish/lgc.c" <<'SRC'
#include "lgc.h"
void luaC_barrier(void *L, void *p, void *v) { (void)L; (void)p; (void)v; }
void luaC_barrierback(void *L, void *p, void *v) { (void)L; (void)p; (void)v; }
void luaC_objbarrier(void *L, void *p, void *v) { (void)L; (void)p; (void)v; }
SRC

  # Fixture LINKER (G24): a mold-shaped linker using ONLY the de-collided linker-
  # implementation tokens (*Section class-name suffixes + get_*_addr accessors). The
  # raw ELF-ABI tokens are deliberately NOT the signal (they also fire on an ELF
  # loader), so this fixture proves the implementation tokens alone select Linker.
  mkdir -p "$tmp/linkerish/src"
  cat >"$tmp/linkerish/src/output-chunks.cc" <<'SRC'
#include "mold.h"
class GotSection { public: int idx; };
class PltSection { public: int idx; };
class GotPltSection { public: int idx; };
class OutputSection { public: int idx; };
class InputSection { public: int idx; };
unsigned long get_got_addr(void *ctx) { return 0; }
unsigned long get_plt_addr(void *ctx) { return 0; }
SRC
  cat >"$tmp/linkerish/src/passes.cc" <<'SRC'
#include "mold.h"
unsigned long get_gotplt_addr(void *ctx) { return 0; }
unsigned long get_tlsgd_addr(void *ctx) { return 0; }
class MergedSection { public: int idx; };
SRC

  # Fixture EMULATOR (G24): an mgba/dosbox-shaped ISA emulator using the de-collided
  # emulator tokens (cartridge/bankswitch/CPU_Cycles/cyclesLate, case-sensitive
  # IO_Read*/IO_Write*, phys_page, vblank/hblank, RaiseIRQ, memory-poke shapes).
  mkdir -p "$tmp/emuish/src/cpu"
  cat >"$tmp/emuish/src/cpu/core.c" <<'SRC'
#include "core.h"
int CPU_Cycles;
int cyclesLate;
unsigned char IO_ReadB(unsigned addr) { return 0; }
void IO_WriteB(unsigned addr, unsigned char v) { (void)addr; (void)v; }
void poke8(unsigned addr, unsigned char v) { (void)addr; (void)v; }
void mem_poke(unsigned addr, unsigned v) { (void)addr; (void)v; }
void RaiseIRQ(int n) { (void)n; }
int phys_page_lookup(unsigned a) { return 0; }
SRC
  cat >"$tmp/emuish/src/cpu/ppu.c" <<'SRC'
#include "ppu.h"
int load_cartridge(const char *rom) { return 0; }
int bankswitch_select(int bank) { return bank; }
int IWRAM[0x8000];
void on_vblank(void) {}
void on_hblank(void) {}
SRC

  # Fixture HIP (G24): a ROCm/HIP host-API repo using the HIP type tokens appended to
  # the gpu pack; must select GPU primary (the HIP -> GPU reclassification).
  mkdir -p "$tmp/hipish/src"
  cat >"$tmp/hipish/src/runtime.cpp" <<'SRC'
#include <hip/hip_runtime.h>
hipError_t run(void) {
    hipStream_t stream;
    hipEvent_t ev;
    void *d;
    hipMalloc(&d, 1024);
    hipMemcpyKind k = hipMemcpyHostToDevice;
    hipMemcpy(d, 0, 1024, k);
    hipMemcpy(0, d, 1024, hipMemcpyDeviceToHost);
    return hipSuccess;
}
SRC

  # Fixture GRAPHICS (iter42): a glfw/GL-shaped windowing app using the collision-safe
  # graphics tokens (the raw gl*/GL_* primitives are intentionally NOT used — they were
  # dropped for colliding on zstd/mold). Must select the Graphics pack as PRIMARY.
  mkdir -p "$tmp/gfxish/src"
  cat >"$tmp/gfxish/src/app.c" <<'SRC'
#include "app.h"
GLFWwindow *win;
int run(void) {
    win = glfwCreateWindow(640, 480, "x", 0, 0);
    glfwMakeContextCurrent(win);
    while (1) { glfwPollEvents(); glfwSwapBuffers(win); }
    return 0;
}
SRC
  cat >"$tmp/gfxish/src/draw.c" <<'SRC'
#include "draw.h"
void frame(void) { nvgBeginFrame(0, 640, 480, 1.0f); nvgBeginPath(0); }
SRC

  # Fixture PROFILER (iter42): a gperftools-shaped profiler using the gperftools-only
  # tokens (google/benchmark API tokens are deferred — shared infra, control collisions).
  mkdir -p "$tmp/profish/src"
  cat >"$tmp/profish/src/prof.c" <<'SRC'
#include "prof.h"
int start(const char *f) { return ProfilerStart(f); }
void stop(void) { ProfilerFlush(); ProfilerStop(); }
int heap(void) { HeapProfilerStart("h"); return IsHeapProfilerRunning(); }
size_t mem(void) { return MallocExtension::instance()->GetAllocatedSize(0); }
SRC

  local gpu_out kernel_out plain_out hpc_out parser_out generic_out comments_out
  local zlibish_out cfsish_out fprimeish_out
  local codec_out net_out cry_out fp2_out
  local db_out pragmaonly_out data_out catch_out deps_out
  local pngish_out rjsonish_out grabbag_out luaish_out
  gpu_out="$(run_detect "$tmp/gpu" no)"
  kernel_out="$(run_detect "$tmp/kernel" no)"
  plain_out="$(run_detect "$tmp/plain" no)"
  hpc_out="$(run_detect "$tmp/hpc" no)"
  parser_out="$(run_detect "$tmp/parser" no)"
  generic_out="$(run_detect "$tmp/generic" no)"
  comments_out="$(run_detect "$tmp/comments" no)"
  zlibish_out="$(run_detect "$tmp/zlibish" no)"
  cfsish_out="$(run_detect "$tmp/cfsish" no)"
  fprimeish_out="$(run_detect "$tmp/fprimeish" no)"
  codec_out="$(run_detect "$tmp/codec" no)"
  net_out="$(run_detect "$tmp/net" no)"
  cry_out="$(run_detect "$tmp/cipher" no)"
  fp2_out="$(run_detect "$tmp/fprime2" no)"
  db_out="$(run_detect "$tmp/db" no)"
  pragmaonly_out="$(run_detect "$tmp/pragmaonly" no)"
  data_out="$(run_detect "$tmp/jsvm" no)"
  catch_out="$(run_detect "$tmp/catchfw" no)"
  deps_out="$(run_detect "$tmp/depsrepo" no)"
  pngish_out="$(run_detect "$tmp/pngish" no)"
  rjsonish_out="$(run_detect "$tmp/rjsonish" no)"
  grabbag_out="$(run_detect "$tmp/grabbag" no)"
  luaish_out="$(run_detect "$tmp/luaish" no)"
  local linkerish_out emuish_out hipish_out gfxish_out profish_out
  linkerish_out="$(run_detect "$tmp/linkerish" no)"
  emuish_out="$(run_detect "$tmp/emuish" no)"
  hipish_out="$(run_detect "$tmp/hipish" no)"
  gfxish_out="$(run_detect "$tmp/gfxish" no)"
  profish_out="$(run_detect "$tmp/profish" no)"
  assetish_out="$(run_detect "$tmp/assetish" no)"

  # Assertion 1: the CUDA fixture selects the GPU pack as primary, anchored to
  # the .cu file. Output format: "pack[<role>]: <label> | <anchor> (N code matches)".
  if ! printf '%s\n' "$gpu_out" | grep -qE 'pack\[primary\]: GPU \(CUDA / SYCL / HIP\) \| src/kernel\.cu:[0-9]+ \([0-9]+ code matches\)'; then
    printf 'cpp_domain_detect self-test: FAIL (CUDA fixture did not select GPU pack)\n'
    printf '%s\n%s\n' '--- gpu ---' "$gpu_out"
    exit 1
  fi
  # Assertion 2: the kernel fixture selects the Kernel pack, anchored to foo.c.
  if ! printf '%s\n' "$kernel_out" | grep -qE 'pack\[primary\]: Kernel / drivers \| drivers/foo\.c:[0-9]+'; then
    printf 'cpp_domain_detect self-test: FAIL (kernel fixture did not select Kernel pack)\n'
    printf '%s\n%s\n' '--- kernel ---' "$kernel_out"
    exit 1
  fi
  # Assertion 3: the plain fixture reports unknown-domain.
  if ! printf '%s\n' "$plain_out" | grep -qF 'unknown-domain: build a pack from references/UNKNOWN-DOMAIN.md'; then
    printf 'cpp_domain_detect self-test: FAIL (plain repo did not report unknown-domain)\n'
    printf '%s\n%s\n' '--- plain ---' "$plain_out"
    exit 1
  fi
  # Assertion 4: plain fixture must NOT print any pack line.
  if printf '%s\n' "$plain_out" | grep -q '^pack\['; then
    printf 'cpp_domain_detect self-test: FAIL (plain repo matched a pack it should not)\n'
    printf '%s\n%s\n' '--- plain ---' "$plain_out"
    exit 1
  fi
  # Assertion 5 (no absolute paths leak): outputs must not contain the temp dir.
  local out
  for out in "$gpu_out" "$kernel_out" "$plain_out" "$hpc_out" \
             "$parser_out" "$generic_out" "$comments_out" \
             "$zlibish_out" "$cfsish_out" "$fprimeish_out" \
             "$codec_out" "$net_out" "$cry_out" "$fp2_out" \
             "$db_out" "$pragmaonly_out" "$data_out" "$catch_out" "$deps_out" \
             "$pngish_out" "$rjsonish_out" "$grabbag_out" "$luaish_out"; do
    if printf '%s\n' "$out" | grep -qF "$tmp"; then
      printf 'cpp_domain_detect self-test: FAIL (absolute path leaked into output)\n'
      printf '%s\n%s\n' '--- out ---' "$out"
      exit 1
    fi
  done

  # -------------------------------------------------------------------------
  # R8 assertions: case-sensitive distinctive uppercase-API tokens.
  # -------------------------------------------------------------------------
  # R8.1: the zlib-like compression fixture must NOT be SPACE-primary — its only
  # SPACE-shaped tokens are the gzip `OS_CODE` and lowercase `os_flush`, which the
  # case-sensitive `OS_(?!CODE\b)`/`OS_API` tokens no longer match.
  if printf '%s\n' "$zlibish_out" | grep -qE 'pack\[primary\]: Space / satellites'; then
    printf 'cpp_domain_detect self-test: FAIL (R8: OS_CODE/os_flush case-fold made a compression lib SPACE-primary)\n'
    printf '%s\n%s\n' '--- zlibish ---' "$zlibish_out"
    exit 1
  fi
  # R9-vocab strengthens R8.1: the inflate/deflate/compress vocabulary now selects
  # the Compression pack as PRIMARY (not merely "not SPACE") — the honest classifier
  # for a compression lib, locking zlib's Networking->Compression reclassification.
  if ! printf '%s\n' "$zlibish_out" | grep -qE 'pack\[primary\]: Compression / codec'; then
    printf 'cpp_domain_detect self-test: FAIL (R9-vocab: zlib-like fixture did not select Compression primary)\n'
    printf '%s\n%s\n' '--- zlibish ---' "$zlibish_out"
    exit 1
  fi
  # R8.2: the cFS-like fixture (real CFE_/OS_ API, no OS_CODE) MUST stay SPACE-primary.
  if ! printf '%s\n' "$cfsish_out" | grep -qE 'pack\[primary\]: Space / satellites'; then
    printf 'cpp_domain_detect self-test: FAIL (R8 over-correction: real cFS CFE_/OS_ repo lost SPACE-primary)\n'
    printf '%s\n%s\n' '--- cfsish ---' "$cfsish_out"
    exit 1
  fi
  # R8.3: the F´-like fixture must be SPACE-primary, NOT Compilers — the CamelCase
  # `FwOpcodeType` TYPE must not match the now-case-sensitive `\bopcode\b`, and the
  # `opcode/bytecode/interpreter` prose lives in a CamelCase `FppTestProject/` test
  # dir excluded by R3+. SPACE must not be lost to a bare opcode match.
  if ! printf '%s\n' "$fprimeish_out" | grep -qE 'pack\[primary\]: Space / satellites'; then
    printf 'cpp_domain_detect self-test: FAIL (R8/R3+: F´ Space lost to a FwOpcodeType/test-dir Compilers match)\n'
    printf '%s\n%s\n' '--- fprimeish ---' "$fprimeish_out"
    exit 1
  fi
  # R3+: no anchor may point into the excluded CamelCase FppTestProject/ test dir.
  if printf '%s\n' "$fprimeish_out" | grep -qE 'FppTestProject/'; then
    printf 'cpp_domain_detect self-test: FAIL (R3+: CamelCase FppTestProject/ test dir not excluded)\n'
    printf '%s\n%s\n' '--- fprimeish ---' "$fprimeish_out"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # R9-vocab assertions: each reclassification proved on the real cloned repos
  # (zlib/lz4 -> Compression, nng -> Networking, blake2 -> Crypto, fprime -> Space)
  # is locked here on a synthetic fixture carrying the same defining vocabulary.
  # -------------------------------------------------------------------------
  # R9.1: the codec fixture selects the NEW Compression pack as PRIMARY off
  # deflate/inflate/compress/crc32/LZ4_/huffman (zlib mis-primaried Networking,
  # lz4 mis-primaried Parser before the pack existed).
  if ! printf '%s\n' "$codec_out" | grep -qE 'pack\[primary\]: Compression / codec \|'; then
    printf 'cpp_domain_detect self-test: FAIL (R9-vocab: codec fixture did not select Compression pack)\n'
    printf '%s\n%s\n' '--- codec ---' "$codec_out"
    exit 1
  fi
  # R9.2: the narrowed Parser `_decode` token must NOT fire on the codec
  # identifier `decode_full_block`/`partial_decode` -> the codec is not Parser.
  if printf '%s\n' "$codec_out" | grep -qE 'pack\[primary\]: Parser'; then
    printf 'cpp_domain_detect self-test: FAIL (R9-vocab: narrowed _decode token still snagged a codec into Parser)\n'
    printf '%s\n%s\n' '--- codec ---' "$codec_out"
    exit 1
  fi
  # R9.3: the messaging fixture selects Networking PRIMARY off the
  # socket/listener/dialer/send/recv idiom (nng lost to Parser before).
  if ! printf '%s\n' "$net_out" | grep -qE 'pack\[primary\]: Networking / protocols \|'; then
    printf 'cpp_domain_detect self-test: FAIL (R9-vocab: socket/listener/dialer fixture did not select Networking)\n'
    printf '%s\n%s\n' '--- net ---' "$net_out"
    exit 1
  fi
  # R9.4: the hash/cipher fixture selects Crypto PRIMARY off primitive names
  # (blake2/sha256/hmac/chacha/poly1305/nonce/digest), not HPC (blake2 lost to HPC).
  if ! printf '%s\n' "$cry_out" | grep -qE 'pack\[primary\]: Crypto \|'; then
    printf 'cpp_domain_detect self-test: FAIL (R9-vocab: hash/cipher fixture did not select Crypto)\n'
    printf '%s\n%s\n' '--- cipher ---' "$cry_out"
    exit 1
  fi
  # R9.5: the F´/F-Prime fixture selects Space PRIMARY off the fprime vocabulary
  # (CCSDS/Framer/Deframer/Tlm/APID/FwOpcode/CmdResponse) with NO cFE OS_/CFE_
  # tokens present — the Space pack was cFE-tuned and blind to F´ before.
  if ! printf '%s\n' "$fp2_out" | grep -qE 'pack\[primary\]: Space / satellites \|'; then
    printf 'cpp_domain_detect self-test: FAIL (R9-vocab: F´ CCSDS/Framer/Tlm/APID fixture did not select Space)\n'
    printf '%s\n%s\n' '--- fprime2 ---' "$fp2_out"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # R12 assertions: the enriched Databases vocabulary wins PRIMARY on a storage
  # engine (sqlite→Databases, redis→Databases, leveldb→Databases on the real repos).
  # -------------------------------------------------------------------------
  # R12.1: the storage-engine fixture selects Databases PRIMARY off the SQL/btree/
  # pager/WAL/vdbe/rowid/PRAGMA + RDB/AOF/keyspace + LSM compaction/memtable vocab,
  # beating the single incidental Parser/Net tokens it also carries.
  if ! printf '%s\n' "$db_out" | grep -qE 'pack\[primary\]: Databases / storage engines \|'; then
    printf 'cpp_domain_detect self-test: FAIL (R12: storage-engine vocab fixture did not select Databases)\n'
    printf '%s\n%s\n' '--- db ---' "$db_out"
    exit 1
  fi
  # R12.2 (over-correction guard): the case-sensitive `(?-i:PRAGMA|Pragma)` token must
  # NOT match the C PREPROCESSOR `#pragma once`/`#pragma clang` — a header full of
  # `#pragma`s but no DB code must NOT be Databases-primary (the Catch2 false-DB case).
  if printf '%s\n' "$pragmaonly_out" | grep -qE 'pack\[primary\]: Databases'; then
    printf 'cpp_domain_detect self-test: FAIL (R12: lowercase #pragma matched the SQL PRAGMA token → false Databases primary)\n'
    printf '%s\n%s\n' '--- pragmaonly ---' "$pragmaonly_out"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # R13 assertions: data-file skip + `#`-comment strip keep a Crypto data-table/
  # build-comment from stealing PRIMARY from the real Compilers/VM domain (duktape).
  # -------------------------------------------------------------------------
  # R13.1: the JS-VM fixture selects Compilers/interpreters/VMs, NOT Crypto — the
  # `\bsha\b` hits live only in the skipped `UnicodeData.txt` data table and a
  # `#`-stripped `Makefile` checksum comment.
  if ! printf '%s\n' "$data_out" | grep -qE 'pack\[primary\]: Compilers / interpreters / VMs \|'; then
    printf 'cpp_domain_detect self-test: FAIL (R13: JS-VM fixture did not select Compilers — data-table/Makefile SHA leaked)\n'
    printf '%s\n%s\n' '--- jsvm ---' "$data_out"
    exit 1
  fi
  # R13.2: the JS-VM fixture must NOT be Crypto-primary (the duktape failure mode).
  if printf '%s\n' "$data_out" | grep -qE 'pack\[primary\]: Crypto'; then
    printf 'cpp_domain_detect self-test: FAIL (R13: Crypto stole primary off UnicodeData.txt "LETTER SHA" / Makefile # SHA1)\n'
    printf '%s\n%s\n' '--- jsvm ---' "$data_out"
    exit 1
  fi
  # R13.3: no anchor may point into the skipped UnicodeData.txt data table.
  if printf '%s\n' "$data_out" | grep -qE 'UnicodeData\.txt'; then
    printf 'cpp_domain_detect self-test: FAIL (R13: UnicodeData.txt data table counted as a code signal)\n'
    printf '%s\n%s\n' '--- jsvm ---' "$data_out"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # R11 assertions: vendored-framework globs anchored to vendored locations — the
  # framework's OWN `src/catch2/` is scanned; an embedded copy under tests/ is not.
  # -------------------------------------------------------------------------
  # R11.1: the framework's own `src/catch2/` source IS now scanned — its tokens
  # (xml_parse/parse_token/json_decode) make a real pack fire anchored to src/catch2/.
  if ! printf '%s\n' "$catch_out" | grep -qE 'src/catch2/'; then
    printf 'cpp_domain_detect self-test: FAIL (R11: framework own src/catch2/ self-excluded — never scanned)\n'
    printf '%s\n%s\n' '--- catchfw ---' "$catch_out"
    exit 1
  fi
  # R11.2: the EMBEDDED vendored copy under tests/vendor/catch2/ must STILL be excluded
  # (its decoy `__global__`/cudaMalloc must NOT make the repo GPU-primary).
  if printf '%s\n' "$catch_out" | grep -qE 'tests/vendor/catch2/|pack\[primary\]: GPU'; then
    printf 'cpp_domain_detect self-test: FAIL (R11: embedded tests/vendor/catch2/ copy not excluded)\n'
    printf '%s\n%s\n' '--- catchfw ---' "$catch_out"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # R10 assertions: bare deps/ + singleheader/ + autosetup/jimsh0.c vendored trees
  # are excluded so the repo's OWN src/ decides the primary (sqlite/redis/simdjson).
  # -------------------------------------------------------------------------
  # R10.1: the deps-bundling repo selects Parser off its OWN src/ JSON parser, NOT
  # Compilers/VMs off the vendored jim.c/jimsh0.c/amalgam interpreter bundle.
  if ! printf '%s\n' "$deps_out" | grep -qE 'pack\[primary\]: Parser / text-format / serialization \|'; then
    printf 'cpp_domain_detect self-test: FAIL (R10: vendored deps/jimsh0/singleheader outvoted the real src/ primary)\n'
    printf '%s\n%s\n' '--- depsrepo ---' "$deps_out"
    exit 1
  fi
  # R10.2: NO anchor may point into deps/, singleheader/, or autosetup/jimsh0.c.
  if printf '%s\n' "$deps_out" | grep -qE 'deps/|singleheader/|autosetup/|jimsh0\.c'; then
    printf 'cpp_domain_detect self-test: FAIL (R10: a vendored deps/singleheader/autosetup/jimsh0 anchor leaked)\n'
    printf '%s\n%s\n' '--- depsrepo ---' "$deps_out"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # P4 assertions: codec-WRAPPER vocab + SIMD-density tie-break + grab-bag dominance
  # floor. Each locks a real reclassification proven on the cloned repos:
  # libpng HPC→Compression, rapidjson HPC→Parser, klib/sds →Generic, lua →Compilers.
  # -------------------------------------------------------------------------
  # P4.1: the PNG-shaped codec selects Compression PRIMARY off the chunk/wrapper
  # vocabulary (IDAT/IHDR/zstream/png_inflate/unfilter/scanline/palette), NOT HPC off
  # its one dense NEON `vld1` filter file (the libpng HPC→Compression reclassification).
  if ! printf '%s\n' "$pngish_out" | grep -qE 'pack\[primary\]: Compression / codec \|'; then
    printf 'cpp_domain_detect self-test: FAIL (P4: PNG-shaped codec did not select Compression — wrapper vocab missing or SIMD density stole it)\n'
    printf '%s\n%s\n' '--- pngish ---' "$pngish_out"
    exit 1
  fi
  # P4.2: the PNG codec must NOT be HPC-primary (the SIMD-density steal it had before).
  if printf '%s\n' "$pngish_out" | grep -qE 'pack\[primary\]: HPC'; then
    printf 'cpp_domain_detect self-test: FAIL (P4: dense 1-file NEON intrinsics stole PRIMARY from a real codec)\n'
    printf '%s\n%s\n' '--- pngish ---' "$pngish_out"
    exit 1
  fi
  # P4.3: the rapidjson-shaped header-only C++ serializer selects Parser PRIMARY off
  # the CamelCase ParseValue/ParseString/SkipWhitespace/kParse vocabulary, NOT HPC off
  # its two SSE2 scan headers (the rapidjson HPC→Parser reclassification).
  if ! printf '%s\n' "$rjsonish_out" | grep -qE 'pack\[primary\]: Parser / text-format / serialization \|'; then
    printf 'cpp_domain_detect self-test: FAIL (P4: rapidjson-shaped serializer did not select Parser — CamelCase parse vocab missing)\n'
    printf '%s\n%s\n' '--- rjsonish ---' "$rjsonish_out"
    exit 1
  fi
  if printf '%s\n' "$rjsonish_out" | grep -qE 'pack\[primary\]: HPC'; then
    printf 'cpp_domain_detect self-test: FAIL (P4: SSE2 scan headers stole PRIMARY from a CamelCase parser)\n'
    printf '%s\n%s\n' '--- rjsonish ---' "$rjsonish_out"
    exit 1
  fi
  # iter43: the cgltf-shaped model importer selects the asset-loader pack PRIMARY off the
  # cgltf_* binary-buffer accessor API, NOT the generic Parser pack.
  if ! printf '%s\n' "$assetish_out" | grep -qE 'pack\[primary\]: 3D asset / model-format import'; then
    printf 'cpp_domain_detect self-test: FAIL (iter43: cgltf-shaped importer did not select asset-loader — out-ranked by Parser?)\n'
    printf '%s\n%s\n' '--- assetish ---' "$assetish_out"
    exit 1
  fi
  if printf '%s\n' "$assetish_out" | grep -qE 'pack\[primary\]: Parser'; then
    printf 'cpp_domain_detect self-test: FAIL (iter43: Parser stole PRIMARY from a real 3D asset loader)\n'
    printf '%s\n%s\n' '--- assetish ---' "$assetish_out"
    exit 1
  fi
  # P4.4: the klib-shaped grab-bag library settles on Generic PRIMARY — the dense
  # 1-file SIMD is blocked by the file-spread floor, and the minority networking module
  # (real socket/connect across 2 files, but dwarfed >10x by the container idioms) is
  # blocked by the 4x dominance floor (the klib/sds →Generic reclassification).
  if ! printf '%s\n' "$grabbag_out" | grep -qE 'pack\[primary\]: Generic library / data-structures / strings \|'; then
    printf 'cpp_domain_detect self-test: FAIL (P4: grab-bag lib did not settle on Generic — a minority/dense pack stole primary)\n'
    printf '%s\n%s\n' '--- grabbag ---' "$grabbag_out"
    exit 1
  fi
  # P4.5: the grab-bag must NOT be HPC (file floor) or Networking (dominance floor) primary.
  if printf '%s\n' "$grabbag_out" | grep -qE 'pack\[primary\]: (HPC|Networking)'; then
    printf 'cpp_domain_detect self-test: FAIL (P4: grab-bag mis-primaried on a 1-file SIMD or a minority networking module)\n'
    printf '%s\n%s\n' '--- grabbag ---' "$grabbag_out"
    exit 1
  fi
  # P4.6: the lua-shaped stack VM selects Compilers PRIMARY — the GC `luaC_barrier`
  # calls no longer match the now word-bounded Filesystems `\bbarrier\b` token, and the
  # opcode/bytecode/interpreter/luaV_/luaK_ vocabulary wins (the lua Filesystems→Compilers fix).
  if ! printf '%s\n' "$luaish_out" | grep -qE 'pack\[primary\]: Compilers / interpreters / VMs \|'; then
    printf 'cpp_domain_detect self-test: FAIL (P4: lua-shaped VM did not select Compilers — barrier token or VM vocab issue)\n'
    printf '%s\n%s\n' '--- luaish ---' "$luaish_out"
    exit 1
  fi
  # P4.7: the lua VM must NOT be Filesystems-primary (the `luaC_barrier` mis-match).
  if printf '%s\n' "$luaish_out" | grep -qE 'pack\[primary\]: Filesystems'; then
    printf 'cpp_domain_detect self-test: FAIL (P4: GC luaC_barrier matched Filesystems barrier token → false Filesystems primary)\n'
    printf '%s\n%s\n' '--- luaish ---' "$luaish_out"
    exit 1
  fi

  # Assertion 6: reproducibility - two consecutive runs byte-match.
  local gpu_out2
  gpu_out2="$(run_detect "$tmp/gpu" no)"
  if [ "$gpu_out" != "$gpu_out2" ]; then
    printf 'cpp_domain_detect self-test: FAIL (two runs did not byte-match)\n'
    diff <(printf '%s\n' "$gpu_out") <(printf '%s\n' "$gpu_out2") || true
    exit 1
  fi

  # Assertion 8 (F2c): the HPC fixture selects HPC. This is the leading-dash
  # `-ffast-math` regression; before the `-e` fix it produced unknown-domain.
  if ! printf '%s\n' "$hpc_out" | grep -qE 'pack\[primary\]: HPC / SIMD / numerics \|'; then
    printf 'cpp_domain_detect self-test: FAIL (HPC -ffast-math/_mm_ fixture did not select HPC: F2c regression)\n'
    printf '%s\n%s\n' '--- hpc ---' "$hpc_out"
    exit 1
  fi
  # Assertion 9 (F2a): the JSON-parser fixture selects the Parser pack as primary.
  if ! printf '%s\n' "$parser_out" | grep -qE 'pack\[primary\]: Parser / text-format / serialization \|'; then
    printf 'cpp_domain_detect self-test: FAIL (JSON parser fixture did not select Parser pack)\n'
    printf '%s\n%s\n' '--- parser ---' "$parser_out"
    exit 1
  fi
  # Assertion 10 (F2a): the container fixture selects Generic as primary.
  if ! printf '%s\n' "$generic_out" | grep -qE 'pack\[primary\]: Generic library / data-structures / strings \|'; then
    printf 'cpp_domain_detect self-test: FAIL (container fixture did not select Generic pack)\n'
    printf '%s\n%s\n' '--- generic ---' "$generic_out"
    exit 1
  fi
  # Assertion 11 (F2b): the single incidental `__packed__` in the container
  # fixture must NOT classify it as Networking (the klib/sds misfire).
  if printf '%s\n' "$generic_out" | grep -qi 'Networking'; then
    printf 'cpp_domain_detect self-test: FAIL (single __packed__ hit misclassified as Networking: F2b)\n'
    printf '%s\n%s\n' '--- generic ---' "$generic_out"
    exit 1
  fi
  # Assertion 12 (F2b): domain tokens that appear only in comments are stripped,
  # so the comment-only fixture reports unknown-domain (no Crypto/Audio/Net pack).
  if ! printf '%s\n' "$comments_out" | grep -qF 'unknown-domain: build a pack from references/UNKNOWN-DOMAIN.md'; then
    printf 'cpp_domain_detect self-test: FAIL (comment-only tokens were not stripped: F2b)\n'
    printf '%s\n%s\n' '--- comments ---' "$comments_out"
    exit 1
  fi
  # Assertion 13 (F2b reproducibility on a ranked output): the HPC fixture's
  # ranked output must byte-match across two consecutive runs.
  local hpc_out2
  hpc_out2="$(run_detect "$tmp/hpc" no)"
  if [ "$hpc_out" != "$hpc_out2" ]; then
    printf 'cpp_domain_detect self-test: FAIL (ranked HPC output not reproducible)\n'
    diff <(printf '%s\n' "$hpc_out") <(printf '%s\n' "$hpc_out2") || true
    exit 1
  fi

  # Assertion 7: JSON mode is well-formed for a match and a non-match.
  local gpu_js plain_js
  gpu_js="$(run_detect "$tmp/gpu" yes)"
  plain_js="$(run_detect "$tmp/plain" yes)"
  case "$gpu_js" in
    \{*\}) : ;;
    *)
      printf 'cpp_domain_detect self-test: FAIL (JSON mode did not emit an object)\n'
      printf '%s\n%s\n' '--- json ---' "$gpu_js"
      exit 1
      ;;
  esac
  if ! printf '%s\n' "$gpu_js" | grep -qF '"matched": true'; then
    printf 'cpp_domain_detect self-test: FAIL (JSON match did not set matched=true)\n'
    printf '%s\n%s\n' '--- json ---' "$gpu_js"
    exit 1
  fi
  if ! printf '%s\n' "$plain_js" | grep -qF '"matched": false'; then
    printf 'cpp_domain_detect self-test: FAIL (JSON non-match did not set matched=false)\n'
    printf '%s\n%s\n' '--- json ---' "$plain_js"
    exit 1
  fi
  if printf '%s\n' "$gpu_js" | grep -qF "$tmp"; then
    printf 'cpp_domain_detect self-test: FAIL (absolute path leaked into JSON)\n'
    printf '%s\n%s\n' '--- json ---' "$gpu_js"
    exit 1
  fi

  # -------------------------------------------------------------------------
  # G24 assertions: the three new/extended packs (linker, emulator, gpu+HIP).
  # -------------------------------------------------------------------------
  if ! printf '%s\n' "$linkerish_out" | grep -qE 'pack\[primary\]: Linker / binary tools / object formats \|'; then
    printf 'cpp_domain_detect self-test: FAIL (G24: linker-implementation tokens did not select Linker primary)\n'
    printf '%s\n%s\n' '--- linkerish ---' "$linkerish_out"
    exit 1
  fi
  if ! printf '%s\n' "$emuish_out" | grep -qE 'pack\[primary\]: Emulator / hardware & ISA emulation \|'; then
    printf 'cpp_domain_detect self-test: FAIL (G24: emulator tokens did not select Emulator primary on an isolated emulator fixture)\n'
    printf '%s\n%s\n' '--- emuish ---' "$emuish_out"
    exit 1
  fi
  if ! printf '%s\n' "$hipish_out" | grep -qE 'pack\[primary\]: GPU \(CUDA / SYCL / HIP\) \|'; then
    printf 'cpp_domain_detect self-test: FAIL (G24: HIP host-API tokens did not select GPU primary)\n'
    printf '%s\n%s\n' '--- hipish ---' "$hipish_out"
    exit 1
  fi
  # iter42: graphics + profiler packs.
  if ! printf '%s\n' "$gfxish_out" | grep -qE 'pack\[primary\]: Graphics / windowing / native UI / rendering \|'; then
    printf 'cpp_domain_detect self-test: FAIL (iter42: graphics tokens did not select Graphics primary)\n'
    printf '%s\n%s\n' '--- gfxish ---' "$gfxish_out"
    exit 1
  fi
  if ! printf '%s\n' "$profish_out" | grep -qE 'pack\[primary\]: Benchmarking / profiling \|'; then
    printf 'cpp_domain_detect self-test: FAIL (iter42: gperftools tokens did not select Benchmarking / profiling primary)\n'
    printf '%s\n%s\n' '--- profish ---' "$profish_out"
    exit 1
  fi

  printf 'cpp_domain_detect self-test: PASS\n'
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

  repo="${repo%/}"
  [ -n "$repo" ] || repo="."

  run_detect "$repo" "$json"
}

main "$@"
