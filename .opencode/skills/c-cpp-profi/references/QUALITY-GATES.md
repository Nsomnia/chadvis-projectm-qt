# Quality Gates

## Purpose

Use this file to choose verification gates for C/C++ work. The project contract wins over these defaults. Do not migrate build systems or add new tools just to satisfy this file unless the user asks.

For the broader expert workflow and tool catalog, read [C-CPP-EXPERT-CANON.md](C-CPP-EXPERT-CANON.md) and [TOOLCHAIN-MATRIX.md](TOOLCHAIN-MATRIX.md). This page is the quick gate selector; those files define the enforcement policy.

## Baseline Inventory

Start with:

```bash
bash .opencode/skills/c-cpp-profi/scripts/cpp_inventory.sh .
bash .opencode/skills/c-cpp-profi/scripts/cpp_gate_plan.sh .
```

Record the compiler, standard, build system, test runner, and whether `compile_commands.json` exists.

For reusable CMake/Meson sanitizer and fuzz scaffolds, read [TOOLCHAIN-TEMPLATES.md](TOOLCHAIN-TEMPLATES.md).

For skill maintenance, run the conformance harness:

```bash
python3 .opencode/skills/c-cpp-profi/scripts/validate_skill_contract.py .opencode/skills/c-cpp-profi
```

## Gate Report

Every non-trivial C/C++ handoff needs a compact evidence packet. Generate the skeleton, then fill in commands and outcomes:

```bash
bash .opencode/skills/c-cpp-profi/scripts/cpp_gate_report.sh .
```

Before claiming completion, validate the filled packet with the applicable risk profile:

```bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py gate-report.md --profile basic
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py gate-report.md --profile parser --profile public-abi
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py gate-report.md --profile parser --require-warning-clean --require-analyzer-review
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py gate-report.md --profile performance --require-performance-proof
```

Profiles are intentionally stricter than prose handoff. `basic` requires inventory, compile, and tests. `memory` adds static analysis and ASan+UBSan. `parser` adds static analysis, ASan+UBSan, and fuzz/corpus evidence. `public-abi`, `performance`, `concurrency`, `refactor`, `native-ui`, `portability`, and `security` each require their matching gate to be passed or the checker fails. For a blocked handoff, use `--allow-failed` only when the final answer clearly says the work is not complete.

Use `--require-warning-clean` when a passed compile gate should mean more than "the compiler produced an artifact." With that flag, the compile evidence must explicitly say `warning-clean: yes`, `warnings: 0`, or `0 warnings`. If warnings remain, mark the gate failed or record the warning count and explain why the handoff is blocked or intentionally risk-accepted.

Use `--require-analyzer-review` for release, parser, memory, security, and review work. With that flag, a passed static-analysis gate must explicitly say `findings: 0`, `no relevant findings`, `findings reviewed: ...`, or `findings triaged: ...`. Static tools often exit `0` while printing warnings, portability errors, or analyzer notes; the evidence packet must prove the output was read.

Use `--require-performance-proof` for every optimization claim. With that flag, a passed performance gate must include baseline or before data, profile or hotspot evidence, an opportunity score, a behavior oracle or isomorphism proof, and after or result data. The checker verifies that the packet is self-contained; it does not verify that the benchmark methodology is strong enough. `--strict-numeric` is an alias for `--require-performance-proof`.

Use `--verify-evidence` to check the *truth* of cited artifacts, not just the *shape* of the report. The flags above confirm a gate is marked passed with non-placeholder command and evidence; they do not confirm the evidence is real. When a gate produces a persisted artifact (a build log, a fuzz reproducer, cross-arch output files, an ABI dump, a golden image), embed a machine-checkable assertion in the evidence cell and the checker re-checks it independently — it cannot be satisfied by writing the wrong value:

- `@verify-exists{path}` — the artifact must exist.
- `@verify-sha256{hex}{path}` — the checker recomputes `sha256(path)` and it must equal `hex`.
- `@verify-contains{path}{substring}` — the artifact must contain the substring (bytes).

Paths resolve against `--verify-base` (default: the report's directory). Example: `cpp_evidence_check.py gate-report.md --profile port --require-transform-proof --verify-evidence --verify-base ./build`. A flipped digit in a claimed digest now fails the report instead of passing it. This closes the artifact-integrity slice of "validate output, not shape." `cpp_evidence_check.py --self-test` exercises the verifier on a real temp file (correct claims pass, tampered claims fail).

Use `--reexec` to validate the *reproducible-command* slice: the author marks an idempotent, safe gate command and states what its output should contain, and the checker re-runs it and confirms.

- `@reexec{<cmd>}{<expected>}` — run `<cmd>` in `--verify-base`; require exit 0, and (when `<expected>` is non-empty) that it appears in the output.

So a claim like "symbol count = 42" backed by `@reexec{nm -D libfoo.so | wc -l}{42}`, or "tests pass" backed by `@reexec{./build/tests}{0 failed}`, is re-run and checked rather than trusted. Re-running is side-effecting, so it is opt-in (the flag plus a per-command directive), bounded by `--reexec-timeout` (default 60s), and a destructive/privileged/network denylist (rm, dd, mkfs, sudo, mv, cp, chmod, curl, ssh, `git reset --hard`, inline-code interpreters like `sh -c`/`python -c`, `find -delete/-exec`, `xargs`, writes to system paths, …) refuses obviously dangerous commands — including path-prefixed forms like `/bin/rm`. That denylist is **best-effort defense-in-depth, not a sandbox**: a denylist over arbitrary shell is fundamentally incompletable (obfuscation always exists), so the real guarantee is that `--reexec` is opt-in and meant for a *trusted* report the author wrote; run it only on trusted reports, ideally inside a container or sandbox. Together, `--verify-evidence` (artifacts) and `--reexec` (reproducible commands) let the checker independently re-verify every claim that *can* be re-verified. The only residual is genuinely non-reproducible output (network, wall-clock, stateful, destructive), which no verifier can re-check; that rests on the honest-reporting contract by necessity, not by omission.

Use `--derive-profiles` to make profile selection derived rather than self-attested. With that flag, the checker reads the `## Change Scope` yes/no answers and computes the minimum required profile set, unions it with any explicit `--profile`, and enforces it:

- `Parser/input/security boundary touched: yes` adds `parser` and `security`.
- `Public API/ABI touched: yes` adds `public-abi`.
- `Threads/locks/atomics/signals touched: yes` adds `concurrency`.
- `Performance claim: yes` adds `performance` and turns on `--require-performance-proof`.
- `Refactor/simplification claim: yes` adds `refactor`.
- `User-visible rendering/artifacts touched: yes` adds `native-ui`.
- `basic` is always included.

Each scope answer that is present must read as `yes` or `no` (a trailing parenthetical note such as `yes (TLV parser)` is allowed); anything else — `maybe`, `sort of`, or free text — fails with `Change Scope: field '<key>' must be yes or no (got '<value>')` so the answers stay machine-usable. This closes the hole where a report that touches a parser or a public ABI could pass on `--profile basic` alone. The `--json` output adds a `derived_profiles` array showing exactly what was derived.

The report must distinguish:

- `passed`: exact command ran and covered the touched behavior.
- `failed`: exact command ran and failed, with the blocking output summarized.
- `not run`: command was applicable but skipped, with the reason.
- `not applicable`: gate does not apply to this change.

Do not let a broad green gate hide a narrow gap. If tests passed but no sanitizer run covered the parser changed in the patch, the dynamic gate is still missing.

Do not let one green tool overrule another tool's concrete finding. Forward testing on real projects found cases where compile, unit tests, and sanitizer tests passed while `clang-tidy`, `cppcheck`, or Valgrind still produced actionable findings. The gate result is only green after the output has been read and classified.

## Multi-Pass Bug Hunt Gate

Use this gate for code review, release hardening, security-sensitive changes, memory/input/concurrency changes, and any request to find bugs deeply.

Once a defect is classified, [REMEDIATION-RECIPES.md](REMEDIATION-RECIPES.md) holds the copy-ready corrected code for each bug class (overflow-checked allocation, bounded copy, RAII/cleanup, false-sharing padding, exception-safe mutators, guarded narrowing, use-after-move/free) and the binary-size methodology; each card names the gate below that proves the fix.

The cycle is:

1. Surface scan: run `ubs` on changed files or the narrow target, plus the project compiler/static analyzer gate.
2. Triage: classify every finding as fixed, false positive with justification, not applicable with reason, or deferred with a bead/follow-up issue.
3. Fix: change one defect class or one root cause at a time.
4. Fresh-eyes reread: re-read every touched file end to end and trace the changed path through callers, callees, headers, build flags, tests, and public API.
5. Dynamic gate: run sanitizer, Valgrind, fuzz, or debugger evidence that matches the risk class.
6. Integration gate: run focused tests plus the affected build/test target.
7. Rescan: rerun the same scanner/tool that found the issue and any gate that could catch regressions from the fix.
8. Stop only when convergence criteria below are satisfied.

Minimum passes by task:

| Task | Required passes |
|---|---|
| Quick pre-commit for docs/scripts only | staged `ubs` or equivalent plus diff review |
| C/C++ feature | compile/tests, fresh-eyes reread, static/risk scan on touched files |
| Crash, memory, parser, or security fix | reproducer, sanitizer/static pass, fresh-eyes reread, regression/fuzz seed, rescan |
| Release or broad audit | surface scan, manual fresh-eyes pass, integration tests, final clean rescan |
| Agent-code review | at least two passes: scanner/tool pass and fresh-eyes manual pass across related files |

Use project gates first, then add risk-specific gates:

| Risk | First gates | Deep gates |
|---|---|---|
| General code | build, tests, `ubs <changed-files>`, `cpp_risk_scan.sh` | fresh-eyes manual trace, focused `clang-tidy`, `cppcheck` |
| Memory/lifetime | ASan+UBSan, Valgrind/Memcheck when useful | ownership graph, cleanup-path review, MSan if dependencies permit |
| Parser/input/protocol | reproducer, regression test, ASan+UBSan fuzzer replay | corpus minimization, boundary corpus, differential/reference checks |
| Integer/bounds | compiler warnings, UBSan, `clang-tidy` | allocation-size multiplication proof, signedness/narrowing trace |
| Concurrency | TSan or Helgrind/DRD when practical | lock-order map, atomic-order proof, callback/signal/loader reentrancy review |
| ABI/API | header compile smoke, symbol diff, layout/API diff | downstream consumer test, docs/examples/manpage consistency |
| Build portability | CMake/Meson/configure matrix, feature probes | compile-only target checks, macro/ifdef review, install/package smoke |

Finding classes to cover explicitly:

- Memory/lifetime: leak, double free, use-after-free, use-after-scope, dangling view, invalid iterator, allocator mismatch.
- UB: signed overflow, shift bounds, alignment, aliasing, pointer provenance, invalid object lifetime, uninitialized read.
- Integer/string: truncation, sign conversion, allocation-size overflow, missing terminator, locale/encoding mismatch, format string.
- Input/parser: unchecked length, partial read/write, recursive depth, resource limit, malformed corpus gap, error recovery.
- Concurrency: data race, lock order, condition-variable predicate, atomic ordering, signal safety, loader/allocator reentrancy.
- ABI/API: symbol/layout/calling convention change, exception crossing ABI, ownership boundary change, build-mode mismatch.
- Portability/build: feature macro drift, generated header mismatch, compile option matrix, C/C++ standard version, platform branch.
- Security: attacker-controlled input, secret exposure, path/temp-file handling, shell/process boundary, hardening regression.

Convergence criteria:

- The same scanner/tool finding no longer reproduces, or the remaining report is documented with a precise false-positive reason.
- Tests covering the touched behavior pass.
- Applicable sanitizer/dynamic/fuzz gates pass or have a tracked, justified gap.
- Fresh-eyes reread found no unresolved sibling bug in the touched file or related boundary.
- No deferred item remains without a bead/follow-up issue.

Record the multi-pass section in the handoff:

```text
Multi-pass audit:
- Scope:
- Passes run:
- Tools run:
- Findings fixed:
- False positives:
- Deferred with issue/bead:
- Manual traces:
- Fresh-eyes files reread:
- Related files traced:
- Reproducers or crash inputs:
- Regression/fuzz/corpus additions:
- Sanitizer/dynamic evidence:
- Static-analysis evidence:
- ABI/API/build-portability evidence:
- Rescan command:
- Convergence status:
- Residual risk:
```

Anti-patterns:

- Fixing analyzer warnings merely because a tool suggested them.
- Letting one green tool override another concrete finding.
- Combining bug fix, refactor, formatting, and optimization in one pass.
- Skipping the fresh-eyes reread after a fix.
- Treating sanitizer success as proof of no UB.
- Suppressing Valgrind or static-analysis output without a local invariant.
- Closing a parser/security crash without minimized input or regression coverage.
- Ignoring ABI, allocator ownership, or build-config changes because tests pass.

## Risk Scan Scope

Use `cpp_risk_scan.sh` as triage:

```bash
bash .opencode/skills/c-cpp-profi/scripts/cpp_risk_scan.sh <changed-files-or-dirs>
```

Prefer changed files, touched directories, or the public API surface under review. Whole-repo scans are useful for orientation, but mature C/C++ repos often contain intentional allocator wrappers, tests, examples, platform shims, and compatibility code. Treat matches as review prompts, not confirmed bugs.

## ABI/API Fallback Snapshot

For shared libraries, plugins, SDKs, FFI boundaries, and public headers, run the project-approved ABI tool first. If no rich ABI tool is available, use the skill fallback snapshot and record that the result is narrower:

```bash
bash .opencode/skills/c-cpp-profi/scripts/cpp_abi_snapshot.sh <candidate-library> [baseline-library]
```

Required interpretation:

- Exact exported-symbol equality is a smoke pass, not ABI compatibility proof.
- Any added, removed, renamed, or newly exported symbol is a review item.
- Review `SONAME`, `NEEDED`, visibility, versioned symbols, language linkage, allocator ownership, exception boundaries, and downstream compile/run evidence.
- If `abidiff`, `abi-dumper`, `abi-compliance-checker`, or `pahole` is missing, write `not run: <tool> unavailable` with why the narrower fallback is or is not acceptable.
- For C++ ABI, explicitly state whether class layout, vtables, inline/template API, RTTI, exception ABI, and standard-library ABI were proven. The fallback snapshot usually does not prove them.

## CMake

Prefer presets when present:

```bash
cmake --version
ctest --version
cmake --list-presets
cmake --list-presets=build
cmake --list-presets=test
cmake --preset <configure-preset>
cmake --build --preset <build-preset>
ctest --preset <test-preset> --output-on-failure
```

Tracked `CMakePresets.json` is project policy. User-specific `CMakeUserPresets.json` should stay untracked.

Without presets:

```bash
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

If `ctest` fails before running tests because PATH resolves to a broken wrapper, locate the CTest binary from the same CMake installation and record the substitution:

```bash
command -v cmake
command -v ctest
$(dirname "$(command -v cmake)")/ctest --version
```

Use the working CTest binary only as an environment fix. Do not treat wrapper failure as project test failure.

## Meson

```bash
meson setup build/debug --buildtype=debug
meson compile -C build/debug
meson test -C build/debug --print-errorlogs
meson introspect build/debug --targets
meson setup build/asan-ubsan --buildtype=debug -Db_sanitize=address,undefined
meson compile -C build/asan-ubsan
meson test -C build/asan-ubsan --print-errorlogs
```

## Make Or Custom Builds

Read `README`, `Makefile`, and CI first. Do not infer targets from names alone.

```bash
make help
make
make test
```

## Strict Diagnostics

Use project-approved warning flags first. If a diagnostic-only local pass is acceptable, prefer:

Clang/GCC C:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Wundef
-Wcast-align -Wstrict-overflow -Wnull-dereference -Wdouble-promotion
```

Clang/GCC C++:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Wundef
-Wcast-align -Wnull-dereference -Wold-style-cast -Wnon-virtual-dtor
-Woverloaded-virtual -Wextra-semi -Wdouble-promotion
```

MSVC:

```text
/W4 /WX /permissive- /analyze
```

Avoid turning on broad new warning sets inside the main build unless you will fix every resulting warning or isolate them in a diagnostic preset.

## Static Analysis

Use at least one when touching memory, input validation, concurrency, security-sensitive code, or public API:

```bash
clang-tidy -p build/debug <files>
scan-build cmake --build build/debug
cppcheck --enable=warning,style,performance,portability --std=c++23 <paths>
```

Use CodeQL or a commercial analyzer when available for security-sensitive changes.

Always inspect analyzer output, not just exit status. Some tools can print error-severity findings while exiting 0, especially when configured for report generation or broad portability analysis. Treat the gate as passed only when the output has been reviewed and the relevant findings are fixed, justified, or moved to tracked follow-up.

## Dynamic Analysis

Minimum sanitizer campaign for memory/input work:

```text
-fsanitize=address,undefined -fno-omit-frame-pointer -g
```

Separate campaigns:

- ASan+UBSan: heap/stack/global OOB, use-after-free, many UB classes.
- LSan: leaks, often integrated with ASan on supported platforms.
- MSan: uninitialized reads, only when all dependencies are instrumented.
- TSan: data races, separate build and run.

Do not combine incompatible sanitizers. Do not link sanitizer runtimes into production builds.

Valgrind/Memcheck can still find issues not surfaced by ASan+UBSan, such as some uninitialized-value flows. When memory risk is high and Valgrind is available, run at least one representative executable:

```bash
valgrind --error-exitcode=99 --leak-check=full <test-or-binary>
```

Record both leak status and error summary.

## Concurrency Gate

For threads, locks, atomics, condition variables, callbacks, signal handlers, constructors/destructors, plugin/FFI boundaries, and shared-state event loops, read [CONCURRENCY-DEADLOCKS.md](CONCURRENCY-DEADLOCKS.md).

Any concurrency claim needs:

1. Thread and ownership map.
2. Lock-order graph for nested locks.
3. Concrete interleaving for each reported race/deadlock/lost wakeup, or a clear statement that no concrete interleaving was found.
4. Atomic memory-order proof for each synchronization edge.
5. TSan, Helgrind/DRD, rr, stress, or debugger evidence when practical.
6. Signal, fork, loader, allocator-hook, and callback reentrancy review when those surfaces exist.

Reject the gate if the finding is only a grep pattern, if the fix is just a timeout around a deadlock, or if `memory_order_relaxed` is changed without proving the stale-read consequence.

## Release Hardening

For security-sensitive release builds, check whether the project can support:

```text
-D_FORTIFY_SOURCE=3
-fstack-protector-strong
-fPIE -pie
-fvisibility=hidden
-Wl,-z,relro,-z,now
-flto=thin
```

For C++ virtual dispatch and indirect-call surfaces, evaluate Clang CFI with LTO and visibility constraints. Record platform and linker limitations.

## Fuzzing Gate

Required for parsers, decoders, compressors, protocol handlers, file formats, and untrusted byte/string APIs. Use libFuzzer, AFL++, honggfuzz, FuzzTest, or the project harness. Always pair fuzzing with sanitizers where possible.

## Performance Gate

Read [PERFORMANCE.md](PERFORMANCE.md) before changing hot-path code. Any optimization claim needs:

1. Baseline command, environment, input, and commit.
2. Profile identifying a real hotspot.
3. Single optimization lever.
4. Behavior oracle captured before edit.
5. Rerun with same benchmark and representative data.
6. Evidence packet with timing, CPU/cache/allocation/syscall evidence where relevant, benchmark limitations, ABI/API impact, and residual risk.
7. Strict evidence check: `cpp_evidence_check.py gate-report.md --profile performance --require-performance-proof`.

Reject the gate if the result depends on changed inputs, changed flags, changed allocator mode, different CPU policy, unsupported target-specific code, or a changed correctness oracle.

## ABI Gate

Required for public headers, shared libraries, plugins, FFI, SDKs, or package exports. Read [BUILD-PORTABILITY.md](BUILD-PORTABILITY.md), then record old/new library paths, headers, compiler, flags, symbol diff, ABI/API checker result, representative layout diff, and downstream compile/run evidence. If `abi-dumper -public-headers` is used, verify Universal Ctags first; otherwise mark the report unfiltered and pair it with explicit exported-symbol and `pahole` type-layout evidence.

## Refactor Isomorphism Gate

For simplify, deduplicate, extract helper, convert ownership, collapse wrappers, template/concept cleanup, macro/build-system cleanup, or any "no behavior change" patch, read [REFACTOR-ISOMORPHISM.md](REFACTOR-ISOMORPHISM.md).

Any refactor claim needs:

1. Baseline before editing: tests, goldens/artifacts, LOC or duplication snapshot, public API/ABI snapshot where applicable.
2. Callsite census for every function/type/header/build target touched.
3. One opportunity score and one lever per commit.
4. Isomorphism card covering outputs, ordering, errors, side effects, ownership, ABI/API/layout, exception safety, concurrency, performance, portability, and artifacts.
5. Before/after gates run on the same inputs.
6. Rejection log for tempting merges that were not actually equivalent.

Reject the gate when the refactor hides a bug fix, changes ABI or ownership without saying so, merges clone type IV/V without proof, removes tests or files without explicit permission, or cannot name all callsites.

## Pixel Or Artifact Gate

For UI/rendering/native graphics/image/video changes, read [NATIVE-UI-GOLDENS.md](NATIVE-UI-GOLDENS.md), then capture before/after golden artifacts. Use perceptual image diff or project-approved pixel thresholds. Verify at every supported scale factor, font stack, color mode, and target platform that matters.

Fallback image comparison:

```bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_pixel_diff.py <baseline-image> <candidate-image> --threshold 0
ffmpeg -hide_banner -i <baseline-image> -i <candidate-image> -lavfi ssim=stats_file=ssim.log -f null -
ffmpeg -hide_banner -i <baseline-image> -i <candidate-image> -lavfi psnr=stats_file=psnr.log -f null -
```

Headless GUI screenshot fallback:

```bash
xvfb-run -a -s "-screen 0 <width>x<height>x24" sh ./capture-golden.sh
ffmpeg -hide_banner -f x11grab -draw_mouse 0 -video_size <width>x<height> -i "$DISPLAY+0,0" -frames:v 1 capture.png
```

The pixel helper is a gate: exit 0 means the artifact is within threshold, exit 1 means visual deltas exceeded threshold, and exit 2 means the comparison itself was invalid. Exact pixel equality is appropriate for deterministic software-rendered fixtures. GUI screenshot evidence must record display backend, screen size/depth, window geometry, capture region, renderer backend, cursor/compositor policy, and comparison command. Antialiasing-tolerant UI needs a justified threshold, FFmpeg SSIM/PSNR evidence, or another project-approved perceptual diff.
