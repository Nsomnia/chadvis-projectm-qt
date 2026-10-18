# C/C++ Expert Canon

## Purpose

This is the enforcing knowledge layer for `c-cpp-profi`. It turns expert C/C++ habits into required agent behavior: study the codebase first, select evidence gates before editing, prove memory and lifetime invariants, measure performance, and record what remains unproven.

The canon is not complete until its claims are forward-tested against real C and C++ projects and kept synchronized with the scripts, templates, and acceptance criteria.

## Non-Negotiable Agent Contract

For any non-trivial C/C++ task, the agent must produce:

1. Boundary map: what changed, public/private surface, ownership boundary, ABI/API impact.
2. Risk map: memory, lifetime, integer, string, aliasing, concurrency, I/O, portability, and security risks.
3. Gate plan before editing: compile, tests, static analysis, dynamic analysis, fuzzing, performance, ABI, portability, and artifact gates as applicable.
4. Evidence report after editing: exact commands, outcomes, failures, skipped gates, and why skipped gates are acceptable or tracked.
5. Residual risk: what was not proven, which follow-up bead tracks it, and whether the code is still safe to land.

No "should be fine" handoff is acceptable.

## Repeat-Skill Extraction Program

The open-source quality target requires repeated application of these sibling skills:

| Pass | Skill | Required output |
|---|---|---|
| 1 | `research-software` | Toolchain matrix, current tool behavior, source anchors, manpage map |
| 2 | `codebase-pattern-extraction` | Patterns from elite C/C++ projects, with paths and commits |
| 3 | `testing-fuzzing` | Fuzz target discovery, harness rules, sanitizer campaigns, crash regression policy |
| 4 | `extreme-software-optimization` | Profile-first performance loop and isomorphism requirements |
| 5 | `multi-pass-bug-hunting` | Audit-fix-rescan convergence rules and fresh-eyes passes |
| 6 | `deadlock-finder-and-fixer` | Lock, atomics, scheduler, signal, loader, and TSan/Helgrind triage |
| 7 | `simplify-and-refactor-code-isomorphically` | Behavior-preserving refactor proof, ABI/layout/timing/golden checks |

The progress ledger is `.skill-loop-progress.md`. It must be updated after each pass.

## Elite Project Study

The following projects were cloned under `/tmp` on 2026-05-24 for pattern extraction. The directories were intentionally left in place because this repo forbids deletion without exact in-session approval.

| Project | Why it matters | Commit | Path |
|---|---|---|---|
| simdjson | performance-critical modern C++ parser with SIMD dispatch, fuzzing, benchmarks, sanitizer policy | `168ef58` | `/tmp/cpp-profi-study-simdjson-20260524` |
| mimalloc | allocator internals, secure mode, heap hardening, platform override, sanitizer/Valgrind integration | `fef6b0d` | `/tmp/cpp-profi-study-mimalloc-20260524` |
| SQLite | high-reliability C, parser/database engine, crash recovery, fault injection, fuzz regression corpus | `3020203` | `/tmp/cpp-profi-study-sqlite-20260524` |
| curl | portable C network library, massive protocol test harness, Valgrind support, docs/manpage checks | `4102400` | `/tmp/cpp-profi-study-curl-20260524` |

### Extracted Patterns

| Pattern | Evidence | Rule for agents |
|---|---|---|
| Reproducible bug reports | simdjson `.github/ISSUE_TEMPLATE/bug_report.md` requires complete test data, reduced examples, compiler/version/optimization settings, and rejects stack-trace-only reports | Do not fix C/C++ crashes from a vague stack trace. Reproduce and minimize first. |
| Performance claims need benchmarks | simdjson `.github/pull_request_template.md` asks for benchmark numbers using high-quality benchmark code; benchmark sources compare against other parsers and include reuse/fair-allocation variants | No optimization claim without baseline, input data, environment, and remeasure. |
| Sanitizers are expected, not optional | simdjson `.github/pull_request_template.md` gives CMake sanitizer commands; mimalloc `CMakeLists.txt` exposes ASan, TSan, and UBSan options | Use sanitizer builds on memory/input/concurrency changes. Record unsupported combinations. |
| Fuzzers are normal build targets | simdjson `fuzz/CMakeLists.txt` builds a named fuzzer set and `all_fuzzers`; SQLite `test/dbfuzz2.c` is a libFuzzer harness with seed guidance | Parser/decoder/protocol code must have narrow harnesses that build regularly enough not to bitrot. |
| Differential fuzzing finds implementation skew | simdjson fuzzers compare minify/parser/UTF-8 behavior across implementations | When multiple implementations or optimized/reference paths exist, fuzz them together and compare outputs. |
| Debug and release ABI/config mixes can be unsafe | simdjson bug template warns not to mix debug and release simdjson code | ABI/config compatibility is part of correctness; record build mode and public-library config. |
| Security/performance tradeoffs are explicit | mimalloc `readme.md` documents secure mode mitigations and warns they are mitigations, not guarantees | Hardening changes must name threat model, residual risk, and cost. |
| Allocators need special dynamic-tool integration | mimalloc `CMakeLists.txt` has `MI_TRACK_VALGRIND`, `MI_TRACK_ASAN`, incompatibility checks, and sanitizer-specific libraries | Allocator/custom memory code needs Valgrind/ASan-aware validation, not generic unit tests only. |
| Runtime invariants can have build levels | mimalloc debug/internal/full options control assertion and expensive invariant checks | Agents should enable stronger invariant builds for risky changes before claiming done. |
| Guard pages can be sampled and reproducible | mimalloc guarded mode documents sample rate, seed, size filters, and precise guard placement tradeoffs | Memory-hardening gates should include reproduction knobs and alignment caveats. |
| Crash and corruption tests become permanent corpus | SQLite `test/dbfuzz2.c`, `test/dbfuzz.c`, and many `test/*corrupt*.test` files encode malformed database/corruption regressions | Every crash input becomes a test or corpus seed. |
| Fuzz harnesses need resource limits | SQLite `test/dbfuzz2.c` sets maximum in-memory DB size and a progress callback to stop runaway SQL | C/C++ fuzz targets must bound input size, time/progress, memory, recursion, and external effects. |
| Feature-flag matrices are tested, not assumed | SQLite `tool/omittest.tcl` enumerates many `SQLITE_OMIT_*` and `SQLITE_ENABLE_*` builds | Compile-time options/macros need matrix testing when they change control flow or public behavior. |
| Assertions encode internal invariants | SQLite source uses dense `assert`, `ALWAYS`, `NEVER`, `testcase`, mutex-held checks, and malloc-failure paths | Internal invariants must be written down or asserted where runtime cost is acceptable. |
| Fault injection is first-class | SQLite tests include malloc/OOM, page-fault, crash, journaling, and corruption scenarios | Critical C systems need failure-path tests, not only happy-path unit tests. |
| Protocol libraries need generated test servers and fixtures | curl `tests/` has protocol servers, fixture data, and `runtests.pl` infrastructure | Network/protocol work needs real fixtures and golden request/response evidence. |
| Documentation is testable | curl `tests/data/test1173` checks manpage syntax; `tests/data/test1177` checks feature names and `CURL_VERSION_*` sync across docs/header/source | Public API docs/examples are gates, not marketing. |
| Dynamic-tool output needs parsers/suppressions | curl `tests/valgrind.pm` and `tests/valgrind.supp` integrate Valgrind into the test system | Serious native projects treat dynamic-tool output as structured test evidence. |
| Portability is a first-class build dimension | curl `configure.ac` probes platform libraries/features/test servers; simdjson has multi-platform CI; mimalloc has platform override paths | Native code must be checked against target compilers/platforms, not the agent's host only. |

### Patterns Not To Generalize Blindly

| Pattern | Why not |
|---|---|
| Dense macro DSLs | SQLite-style macros work because maintainers understand the engine deeply; new projects can make code less readable and harder for analyzers. |
| Custom allocators everywhere | mimalloc-level allocator work is specialized; most projects should prefer clear ownership and standard allocators unless profiling/security requires otherwise. |
| SIMD specialization first | simdjson-style dispatch is justified by parser hot paths and benchmark culture; normal code should profile before adding ISA-specific branches. |
| Huge compile-option matrices | SQLite's option matrix is valuable because SQLite is embedded everywhere; small projects should test meaningful supported configs, not invent unused permutations. |
| Giant bespoke test harnesses | curl's harness is justified by protocol surface area; smaller projects should adopt focused fixtures before building infrastructure. |
| Suppressions as cleanup | Valgrind/static-analysis suppressions are acceptable only with comments and ownership; they must not hide unknown findings. |

### Elite Project Invariants

These line-backed patterns were extracted with `codebase-pattern-extraction` during pass 2.

| Invariant | Evidence | Enforcing rule |
|---|---|---|
| Admit changes only with objective evidence | simdjson `CONTRIBUTING.md:55-73`; curl `docs/CONTRIBUTE.md:64-82`; curl `docs/CODE_REVIEW.md:13-23` | Every agent change must state change class, proof target, and smallest necessary diff. Style taste or broad cleanup is not evidence. |
| Minimized reproducers become permanent assets | simdjson `.github/ISSUE_TEMPLATE/bug_report.md:37-42`; SQLite `test/fuzzcheck.c:60-72`; curl `tests/data/test663:1-78` | Crash, parser, security, and corruption fixes require a reduced input or scripted reproducer in the regression/fuzz suite. |
| Sanitizers and dynamic tools are first-class build modes | simdjson `.github/workflows/ubuntu24-sani.yml:17-55`; SQLite `main.mk:2264-2281`; mimalloc `CMakeLists.txt:225-260`; curl `docs/tests/CI.md:62-70` | Run native ASan/UBSan/MSan/TSan/Valgrind gates where applicable and record unsupported combinations. |
| Fuzzing has lifecycle, not just targets | simdjson `fuzz/Fuzzing.md:27-99`; SQLite `main.mk:924-950`; SQLite `test/dbfuzz2.c:13-37`; curl `docs/INFRASTRUCTURE.md:49-55` | Parser/protocol/file-format work needs target selection, corpus replay, minimization, sanitizer replay, and crash-to-regression handling. |
| Optimized backends need scalar/reference oracle | simdjson `fuzz/CMakeLists.txt:57-69`; simdjson `cmake/implementation-flags.cmake:4-20`; simdjson `src/implementation.cpp:211-342` | SIMD or platform-specialized code must keep a portable fallback and differential tests against reference behavior. |
| Unsafe API states should fail at compile time when possible | simdjson `tests/compilation_failure_tests/CMakeLists.txt:1-37`; simdjson `tests/compilation_failure_tests/dangling_parser_parse_stdstring.cpp:7-15` | Lifetime and ownership misuse that the type system can reject should get compile-fail tests. |
| Portability is a tested matrix with honest skips | simdjson `.github/workflows/ubuntu24.yml:10-26`; simdjson `.github/workflows/rvv-128-clang-20.yml:16-39`; curl `configure.ac:337-460`; curl `CMakeLists.txt:133-170` | Distinguish host, target, compiler, feature probes, compile-only checks, and executed tests. |
| Installed-consumer tests catch packaging lies | simdjson `tests/installation_tests/README.md:1-5`; simdjson `.github/workflows/ubuntu22-cxx20.yml:52-65`; curl `scripts/installcheck.sh:38-52` | Public headers, CMake/pkg-config, install layout, and exported libraries need downstream smoke tests after installation. |
| ABI/API is artifact-tracked | curl `docs/CODE_REVIEW.md:39-47`; curl `configure.ac:2722-2773`; curl `tests/data/test1119:1-25`; mimalloc `include/mimalloc.h:17-118` | Public API changes require symbol/version/header/doc checks, compatibility notes, and explicit ownership/allocation boundaries. |
| Docs are part of the test surface | curl `Makefile.am:88-138`; curl `docs/libcurl/Makefile.am:39-58`; curl `tests/data/test1139:1-25`; curl `tests/data/test1488:1-25` | Public options, API docs, generated manpages, examples, and version metadata should be checked by automation. |
| Protocol tests need executable transcripts | curl `docs/tests/FILEFORMAT.md:7-77`; curl `tests/data/test663:1-78`; curl `configure.ac:337-477` | Protocol changes need fixtures with server behavior, client command, expected wire transcript, feature gating, and real test-server detection. |
| Internal invariants should be written down and attacked | SQLite `doc/pager-invariants.txt:1-76`; SQLite `doc/testrunner.md:160-182`; SQLite `test/btreefault.test:12-102`; curl `docs/CODE_REVIEW.md:63-72` | Allocator, pager, parser, and protocol internals need invariant docs, debug assertions, fault injection, and corruption/crash tests. |
| Allocator hardening has threat, cost, and tooling contracts | mimalloc `readme.md:407-460`; mimalloc `readme.md:572-640`; mimalloc `src/alloc.c:650-723` | Allocator/security modes must state threat model, overhead, ABI/alignment impact, environment knobs, and Valgrind/ASan compatibility. |
| Performance claims require representative methodology | simdjson `CONTRIBUTING.md:67-69`; simdjson `.github/workflows/ubuntu24-checkperf.yml:29`; mimalloc `readme.md:668-694` | Before/after numbers must name workload, environment, variance controls, and limits of synthetic benchmarks. |
| C hot-path review has recurring failure modes | curl `docs/CODE_REVIEW.md:74-90`; curl `docs/CODE_REVIEW.md:139-160` | Proactively review hot-path allocation, static state, integer overflow, unaligned access, zero-termination assumptions, and raw growing-buffer handling. |

## C Internals That Must Be Covered

| Area | Expert checks |
|---|---|
| Object lifetime | allocation source, ownership transfer, cleanup label, double-free proof, leak proof |
| Bounds | every pointer+length pair, sentinel assumptions, integer narrowing before indexing |
| Undefined behavior | signed overflow, shift bounds, alignment, effective type, strict aliasing, uninitialized reads, invalid pointer provenance |
| Strings | bounded length, encoding assumptions, null termination, locale effects, `snprintf` result handling |
| Integers | width, signedness, conversion, multiplication overflow, allocation-size overflow |
| Error paths | cleanup idempotence, partial initialization, errno preservation, retry policy |
| ABI | symbol visibility, struct layout, enum width, calling convention, allocator ownership, version scripts |
| Preprocessor | feature-test macros, platform branches, ODR risk in headers, macro side effects |
| I/O | short reads/writes, EINTR/EAGAIN, fsync semantics, atomic rename, path encoding |
| Signals | async-signal safety, reentrancy, thread masks, no allocation in handlers |
| Concurrency | pthread contract, atomics order, data race proof, lock order, condition-variable loop, cancellation cleanup |

## C++ Internals That Must Be Covered

| Area | Expert checks |
|---|---|
| RAII | owning handles, move-only types, deterministic cleanup, no raw owning `new`/`delete` |
| Exception safety | no-throw/destructor rules, strong/basic guarantee, rollback plan, FFI exception boundary |
| Value categories | move/copy cost, dangling references, forwarding references, lifetime extension traps |
| Templates | concept/constraint clarity, instantiation bloat, error quality, ODR, ABI exposure |
| Polymorphism | virtual destructor, slicing, vtable ABI, CFI suitability, downcast proof |
| Standard library | iterator invalidation, string_view/span lifetime, allocator propagation, container stability |
| Concurrency | `std::atomic` memory order, `std::mutex` lock order, condition variables, thread lifetime |
| Modules/headers | self-contained headers, include cost, export surface, macro isolation |
| Coroutines | lifetime, cancellation, allocator use, promise type, scheduler ownership |
| Interop | `extern "C"`, name mangling, STL across ABI boundary, allocator ownership |

## Enforced Workflows

### New feature

1. Inventory.
2. State invariants and public surface.
3. Add or update focused tests.
4. Compile with project settings.
5. Run static analysis on changed files.
6. Run sanitizer or dynamic gate when memory/input/concurrency touched.
7. Update docs/examples for public API.
8. Record ABI/API impact.

### Crash or memory bug

1. Reproduce with exact command/input.
2. Minimize input.
3. Run ASan+UBSan and, if useful, Valgrind/Memcheck.
4. Inspect ownership/lifetime path manually.
5. Fix one root cause.
6. Add regression test or fuzz seed.
7. Re-run original reproducer, sanitizer, tests, and static analysis.

### Multi-pass bug hunt

1. Start with tools and tests, but do not delegate judgment to them.
2. Run a surface scan on the narrowest useful target: `ubs`, compiler diagnostics, static analyzer, or project-specific checker.
3. Classify every finding as fixed, false positive with justification, not applicable with reason, or deferred with a bead/follow-up issue.
4. Fix one root cause at a time, then rerun the exact tool or reproducer that found it.
5. Re-read every touched C/C++ source, header, build file, and test with fresh eyes after the fix.
6. Trace related callers, callees, public headers, ownership boundaries, generated config, and build flags before claiming the defect is isolated.
7. Run the dynamic gate that matches the risk class: ASan/UBSan for memory/UB, MSan for uninitialized flow, TSan/Helgrind/DRD for races, Valgrind for uninitialized/leak paths, fuzzing for untrusted input.
8. Repeat audit, fix, and rescan until no new finding appears.
9. Stop only after the rescan is clean or justified, focused tests pass, no fresh-eyes sibling bug remains, false positives have evidence, and all deferred items are tracked.

Required bug classes:

| Class | Native checks |
|---|---|
| Memory/lifetime | owner/release path, cleanup idempotence, invalidation, allocator family, destructor/RAII path |
| UB | overflow, shift, alignment, aliasing, pointer provenance, object lifetime, uninitialized read, data race |
| Integer/string | width, sign, allocation multiplication, bounded copies, terminator, format/varargs, encoding/locale |
| Input/parser | length trust, recursion/resource limits, malformed corpus, recovery path, partial I/O |
| Concurrency | lock order, wait predicates, atomics order, cancellation, signal safety, loader/allocator callbacks |
| ABI/API | symbols, layout, calling convention, exception boundary, ownership transfer, C/C++ linkage |
| Portability/build | feature probes, macro matrix, generated headers, standard version, compiler-specific extensions |
| Security | attacker boundary, hardening flags, secret exposure, path/process boundary, threat model |

### Parser, decoder, file format, protocol, compression

1. Identify untrusted byte/string entry points.
2. Add or improve a narrow harness at the parser boundary.
3. Seed valid, invalid, boundary, and known-regression corpora.
4. Run ASan+UBSan fuzzer.
5. Consider MSan if uninitialized data risk is high and dependencies can be instrumented.
6. Minimize crashes before debugging.
7. Add every crash as a regression.

### Performance change

1. Capture baseline with command, inputs, CPU, compiler, flags, and commit.
2. Profile and identify hotspot.
3. Score candidate: impact times confidence divided by effort.
4. Change one lever only.
5. Prove behavior unchanged using tests, golden outputs, API/ABI checks, ownership/lifetime review, backend fallback checks, UB notes, and floating-point tolerance.
6. Rerun same benchmark and profile.
7. Reject faster results when the benchmark inputs, CPU policy, flags, allocator mode, oracle, ABI/API, or supported targets changed without an explicit contract change.

### Refactor

1. Read [REFACTOR-ISOMORPHISM.md](REFACTOR-ISOMORPHISM.md).
2. Prove behavior equivalence before edit.
3. Preserve ABI/API unless the task explicitly changes it.
4. Preserve error semantics, ordering, side effects, logging, timing assumptions, allocation ownership, exception safety, and destructor/copy/move behavior.
5. Prefer net-negative complexity, not abstraction for its own sake.
6. Run old and new behavior evidence on the same inputs.
7. Reject the refactor if the callsite census, ABI/layout proof, or golden/regression evidence is missing.

### Concurrency

1. Read [CONCURRENCY-DEADLOCKS.md](CONCURRENCY-DEADLOCKS.md).
2. Build a lock/thread ownership map.
3. Check all nested locks for consistent order.
4. Require a concrete interleaving before reporting a race or deadlock.
5. Run TSan or Helgrind/DRD when practical.
6. For atomics, document the synchronization relation; use stronger ordering when proof is weak.
7. Check signal/loader/allocator reentrancy for callbacks and interposed libraries.

## Evidence Packet Template

```markdown
## C/C++ Evidence

Boundary:
- Public API:
- ABI:
- Ownership:
- Platform assumptions:

Risk map:
- Memory:
- UB:
- Integer/string:
- Concurrency:
- Security:
- Performance:
- Portability:

Commands:
| Gate | Command | Result | Notes |
|---|---|---|---|

Unproven:
- [gap] tracked by [bead/id]
```

## Completion Standard

The skill is not "12/10" until:

- At least three elite C/C++ projects have been cloned, studied, and summarized with reusable patterns.
- The toolchain matrix covers compiler, build, static, dynamic, fuzz, ABI, profiling, docs, portability, and hardening tools.
- The skill routes agents to existing sibling skills for fuzzing, optimization, bug hunting, deadlocks, and isomorphic refactoring.
- Missing tools produce explicit gaps, not silent skips.
- Forward tests cover CMake, Meson, sanitizer, fuzz, ABI, native UI/golden, and at least one memory-debugging gate.
- A conformance harness verifies reference links, scripts, acceptance criteria, and examples stay synchronized.
