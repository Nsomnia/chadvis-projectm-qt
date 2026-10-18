# Testing And Fuzzing

## Test Shape

Use multiple oracles:

- Unit tests for local invariants.
- Integration tests for API and ABI behavior.
- Regression tests for every crash, sanitizer finding, and parser bug.
- Property tests for algebraic or state invariants.
- Metamorphic tests when exact expected output is hard.
- Differential tests against a reference implementation when available.
- Golden artifact tests for rendered pixels, serialized bytes, protocol messages, and CLI output.

## What Must Be Fuzzed

Fuzz any public or internal boundary that consumes:

- Raw bytes, strings, files, packets, frames, archives, images, audio/video, compression streams.
- Config files, JSON, XML, YAML, protobuf, FlatBuffers, SQL-like languages.
- Regex-like languages, expressions, templates, query languages.
- Network protocol messages or stateful command sequences.
- Values crossing FFI from less trusted runtimes.

## C/C++ Fuzz Harness Rules

- Parser/file/protocol changes are incomplete without target discovery evidence.
- A green unit suite does not satisfy the fuzz gate unless the touched input boundary was executed by a fuzz smoke, corpus replay, or regression reproducer.
- Required parser throughput floor: 1000 exec/s for byte/string parsers, 100 exec/s for stateful protocol or API fuzzers. Below that, fix harness overhead before trusting campaign depth.
- Target the smallest parser or state-transition boundary.
- Every harness must name its oracle: crash-only, invariant, round-trip, differential, metamorphic, protocol transcript, or resource-bound oracle.
- Every harness must name its bounds: max input length, recursion/depth limit, allocation cap, progress/time limit, and external effects.
- Move one-time initialization out of the fuzz body.
- Bound memory, recursion, progress callbacks, and input sizes deliberately.
- Avoid filesystem, network, real time, sleeping, randomness, and global mutable state in the hot fuzz path.
- Maintain at least two libFuzzer build modes when practical: fast exploration for corpus growth and sanitized replay for bug detection.
- Pair fuzzing with sanitizers, usually ASan+UBSan first. **Build UBSan with `-fno-sanitize-recover=all` and run with `UBSAN_OPTIONS=halt_on_error=1` (and `print_stacktrace=1`).** By default UBSan only *prints* and then *recovers*, so a coverage-guided campaign keeps running past real UB (misaligned/unaligned loads, signed overflow, invalid enum) and the run still "passes" — a verified blind trial missed a genuine misaligned-load UB in a glTF accessor reader for exactly this reason until the recover-off flags were added. Without halt-on-error, UBSan is an advisory printout, not a gate.
- Deep-nesting blind spot: coverage-guided mutation rarely produces the tens of thousands of repeated tokens (`[[[[…`, `{{{{…`, `((((…`) that trigger unbounded-recursion stack-overflow in a recursive-descent parser, so a clean fuzz campaign does NOT clear that hazard. Add directed depth-structured seeds (and read the parser for an uncapped recursive call) — see Recipe 10. Two of three defects a blind trial found across unseen parsers were this class.
- Minimize crashing inputs before debugging.
- Convert every crash into a regression test.
- Use differential or metamorphic oracles when exact expected output is hard.
- Keep fuzz targets building in normal developer or CI workflows so they do not bitrot.

## Target Discovery

Run discovery before writing a harness:

```bash
rg -n 'const (uint8_t|unsigned char|char) \\*|void \\*|std::span|std::string_view|std::istream|FILE \\*|read\\(|parse|decode|deserialize|load|from_' \
  --glob '*.{c,h,cc,cpp,cxx,hpp,hxx}' .
rg -n 'memcpy|memmove|strcpy|strncpy|strcat|sprintf|snprintf|sscanf|realloc|malloc|new\\[|delete\\[' \
  --glob '*.{c,h,cc,cpp,cxx,hpp,hxx}' .
rg -n 'LLVMFuzzerTestOneInput|afl|honggfuzz|corpus|seed|crash|regress' .
```

Scan and score more than function names. Include byte/length pairs, input adaptors (`FILE*`, streams, custom readers, mmap views), parser verbs, risk multipliers, and feature/backend matrices.

Score each candidate:

| Target | Trust boundary | Input shape | Oracle | Bounds | Sanitizers | Score | Selected? |
|---|---|---|---|---|---|---:|---|

Fuzz highest scores first. Do not fuzz the whole application when a parser function can be called directly.

Use this scoring guide:

- Consumes untrusted bytes/strings/files/network data: +3.
- Parser/decoder/compressor/protocol/state machine: +3.
- Uses raw memory, pointer arithmetic, casts, custom allocator, or C ABI: +2.
- Has prior crash, sanitizer, CVE, or corruption history: +3.
- Has reference implementation, round-trip, or differential oracle: +1.
- Is easy to isolate without I/O/global state: +1.

If a higher-scored target is not selected, record why.

## Harness Gotchas

- Use `LLVMFuzzerInitialize` or static one-time initialization for dictionaries, backend lists, lookup tables, and expensive config.
- If the API mutates input, copy into owned mutable storage before calling it.
- Never pass fuzz bytes as C strings unless the harness deliberately adds and documents a terminator.
- Read integers with `memcpy`, not unaligned casts.
- Split multi-part inputs with a documented separator or structured provider.
- Seed deterministic PRNGs and disable nondeterministic host behavior.
- For stateful APIs, reset the object graph per iteration or prove isolation.
- For database, protocol, VM-style, or query-engine targets, add progress callbacks or step limits.
- For allocator/custom memory work, add leak checks and independent Valgrind/Memcheck replay when available.

## Sanitizer Campaign Matrix

| Campaign | Use for | Notes |
|---|---|---|
| Fast fuzz | corpus growth and coverage discovery | record exec/s, edges/features, max length, duration |
| ASan+UBSan | first-line memory, bounds, use-after-free, many UB classes | default for parser, decoder, file-format, compression, and C API fuzzing |
| O0 sanitized replay | debugger-friendly reproduction | minimized input must still reproduce |
| LSan | leak-sensitive libraries and long-running tools | often integrated with ASan on Linux; record if unsupported |
| MSan | uninitialized-read risk | only valid when dependencies and libc surface are instrumented enough to avoid false positives |
| TSan | threads, atomics, callbacks, async/native bridge, lock-free structures | separate build and run; do not combine with ASan |
| Valgrind/Memcheck | allocator work, uninitialized-value paths, independent memory evidence | slower; run corpus replay or representative tests |
| Platform/backend fuzz | SIMD, endian, architecture, feature flags | backend list and differential oracle result |
| CFI/HWASan/platform sanitizers | virtual dispatch, pointer authentication, mobile/Android, hardened builds | require platform/compiler support and explicit notes |

Do not combine incompatible sanitizers. Do not report "sanitizer clean" unless the touched behavior actually executed.

## Corpus Lifecycle

1. Seed with valid minimal inputs, malformed inputs, boundary sizes, empty input, max supported size, and known regressions.
2. Add dictionaries for structured text or binary formats.
3. Run short smoke locally before handoff.
4. Run longer campaigns in nightly/release tiers.
5. Minimize corpora after growth:

```bash
./fuzz_target -merge=1 corpus new_corpus
llvm-profdata merge -sparse *.profraw -o fuzz.profdata
llvm-cov report ./fuzz_target -instr-profile fuzz.profdata
```

6. Keep crashes separate from the main corpus until triaged.
7. Promote fixed crashes into regression tests or permanent corpus seeds.
8. Deduplicate crashes by top stack frames plus target/oracle class, not file name.

## Crash Triage

Use this order:

```bash
./fuzz_target crash-input -runs=1
./fuzz_target -minimize_crash=1 crash-input
./fuzz_target minimized-crash -runs=1
ASAN_OPTIONS='abort_on_error=1:symbolize=1' ./fuzz_target minimized-crash -runs=1
```

Classify:

| Class | Required action |
|---|---|
| Memory safety or UB | fix root cause, add regression, rerun sanitizer corpus |
| Assertion or invariant violation | decide whether assertion reflects public contract or internal bug; add regression |
| Timeout or OOM | add progress/memory bounds, then retest minimized case |
| Differential mismatch | preserve both inputs and outputs, identify reference authority |
| False positive | document why, add harness guard only if it preserves real coverage |

## Regression Policy

Every real finding needs a permanent test:

- Unit test for deterministic API-level bug.
- Corpus seed for parser/fuzzer-only crash.
- Protocol transcript or golden artifact for wire/rendering bugs.
- Compile-fail test for C++ lifetime/API misuse that should be impossible.
- Failure-injection test for OOM, short read/write, interrupted syscall, or corruption path.

If a crash cannot be committed because it is huge or sensitive, commit a generator/minimizer script or a synthetic equivalent.

## libFuzzer Skeleton

```cpp
#include <cstdint>
#include <cstddef>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Call the smallest input boundary. Reject only impossible sizes.
    return 0;
}
```

Build shape:

```bash
clang++ -g -O1 -fsanitize=fuzzer,address,undefined target.cc lib_under_test.a -o fuzz_target
./fuzz_target corpus/ -max_total_time=60
```

Reusable harness and build fragments are in `assets/fuzz/`, `assets/cmake/`, and `assets/meson/`. Read [TOOLCHAIN-TEMPLATES.md](TOOLCHAIN-TEMPLATES.md) before copying them into a target repo.

## Evidence Packet

```markdown
## Fuzz/Sanitizer Evidence

Target discovery:
- Candidates scanned:
- Selected target and score:
- Reason not fuzzing higher-scored candidates:
- Boundary:

Harness:
- Path:
- Oracle:
- Bounds:
- One-time initialization:

Campaigns:
| Campaign | Command | Runs/time | Exec/s | Corpus | Result | Notes |
|---|---|---:|---:|---|---|---|

Corpus:
- Seeds:
- New coverage:
- Minimized:
- Crashes:
- Regressions added:
- Dictionary:

Crash triage:
- Original artifact:
- Minimized artifact:
- Stack hash:
- Classification:
- Root cause:
- Regression asset:

Unproven:
- Missing sanitizer/tool/platform:
- Follow-up bead:
```

## FuzzTest

For C++ projects already using GoogleTest or willing to add a property-style harness, FuzzTest can express typed property tests backed by coverage-guided fuzzing.

## Golden Pixels And Artifacts

For native UI, graphics, imaging, text shaping, terminal rendering, video, or plot output:

- Capture deterministic fixtures.
- Freeze fonts, locale, DPI/scale, color management, and platform if possible.
- Use pixel or perceptual diff thresholds approved by the project.
- Store failure artifacts for inspection.

## CI Tiers

- Per change: compile, focused tests, quick sanitizer target, short fuzz smoke for touched parsers.
- Nightly: full sanitizer matrix, longer fuzz campaigns, coverage report, slow integration.
- Release: clean full test suite, sanitizer suite, corpus replay, ABI/API checks, performance baseline.
