# Performance

## Principle

C/C++ earns its advantage only when the implementation is measured and the abstractions compile away. A performance edit without a baseline and profile is speculation.

## Loop

1. Define the workload and user-visible metric: latency, p95, p99, throughput, frame time, memory, binary size, startup, power, tail jitter.
2. Capture baseline with environment details.
3. Profile and rank hotspots.
4. Pick one lever with an opportunity score.
5. Prove behavior unchanged before editing.
6. Re-run the same benchmark and compare.
7. Record the result, confidence, and residual risk.

Do not accept "tool suggested it", "this looks faster", or "modern C++ should optimize it" as a reason to change code. simdjson's contribution rules are a good model: performance work needs hard numbers, and broad taste-driven rewrites are not evidence.

## Quick Optimization Card

Use this compact card in the handoff for every performance patch:

```text
Optimization claim:
- Baseline: <command, input, build, p50/p95/p99 or throughput, memory if relevant>
- Profile: <tool and top hotspot in changed path>
- Opportunity score: <impact * confidence / effort = score, must be >= 2.0>
- Oracle/isomorphism: <goldens/checksums/corpus/differential/ABI/numeric/error-ordering proof>
- One lever: <single optimization mechanism>
- After/result: <same benchmark and profile comparison>
- Native risk: <UB, FP, ABI/API, allocator, SIMD fallback, portability, p99/worst-case>
- Residual risk: <what was not proven>
```

Machine check:

```bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py gate-report.md --profile performance --require-performance-proof
```

The strict checker only validates evidence shape. The agent must still inspect the actual benchmark/profile output and reject weak data.

## Absolute Rules

- No performance patch is allowed without a baseline, a profile, and a behavior oracle captured before editing.
- The profile must identify a real hotspot in the changed path. Style, tool suggestions, and intuition are not performance evidence.
- Optimize one lever per commit. If two levers are needed, split them so the benchmark delta and regression risk stay attributable.
- Faster output is not acceptable if ordering, errors, ABI/API, allocator ownership, floating-point behavior, or fallback behavior changed without an explicit contract change.
- Sanitizer success is not proof that UB is impossible. Use it as one input alongside manual review of aliasing, alignment, pointer provenance, overflow, and data races.

## Benchmark Hygiene

Record:

- CPU model, core count, governor/power mode, thermal state if relevant.
- OS/kernel/libc, compiler, linker, build type, flags, target architecture, sanitizer state.
- Input corpus, dataset size, warmup, repetitions, confidence/variance, and whether caches are hot or cold.
- Allocation mode: fresh allocation, buffer reuse, allocator override, huge pages, NUMA, thread count.
- Whether the benchmark is synthetic, micro, integration, production replay, or worst-case stress.

Rules:

- Benchmark release-like builds unless debug performance is the product.
- Keep before/after inputs and environment identical.
- Include negative and tail metrics when relevant, not only best throughput.
- For allocator, parser, protocol, and SIMD work, include at least one representative real workload and one targeted stress case.
- If a benchmark uses buffer reuse, also consider a fair fresh-allocation variant so the result does not hide allocation cost.
- State the limits of the benchmark. Mimalloc's own docs explicitly warn that benchmark suites may not cover long-running server workloads or worst-case latency.

## Baseline And Profiling Matrix

| Dimension | Required evidence | Useful tools | Reject if |
|---|---|---|---|
| Workload | command, args, input corpus, data size, hot/cold cache state | project runner, replay fixture, Google Benchmark | synthetic-only result for a production claim |
| Environment | CPU model, core count, SMT, governor, turbo, thermal notes, NUMA, OS/kernel/libc | `lscpu`, `uname -a`, `numactl -H`, platform equivalents | before/after ran on different machines or policies |
| Build | commit, compiler/linker, build type, flags, target arch, LTO/PGO/BOLT state, sanitizer state | CMake/Meson logs, `compile_commands.json` | debug/sanitized build used for release claim |
| Timing | min/median/p95/p99, throughput, variance, warmup, repetitions | `hyperfine`, Google Benchmark, `perf stat -r` | best-of-one or no variance reported |
| CPU profile | top functions/lines with inclusive and self time | `perf record/report`, VTune, Instruments, WPA, callgrind | target function is not in top hotspots |
| Memory | allocation count, bytes, churn, peak RSS, fragmentation risk | heaptrack, massif, allocator stats, ETW | allocator swap claimed without allocation evidence |
| Cache/branch | cache misses, branch misses, icache/code-size pressure | `perf stat -d`, cachegrind, BOLT reports | cache-locality claim without counters or profile shift |
| Syscalls/I/O | syscall count, short I/O, page faults, `mmap`, `fsync` behavior | `strace -c`, `perf trace`, platform equivalents | throughput claim ignores syscall or page-fault regression |
| Threads | contention, cross-thread frees, false sharing, tail latency | perf lock, TSan, Helgrind/DRD, Tracy | average improves while p99/worst-case degrades |
| Backend/SIMD | selected backend, dispatch condition, scalar/reference comparison | backend toggles, differential tests, disassembly when relevant | optimized backend lacks fallback proof |

## Tools

```bash
hyperfine --warmup 3 --runs 10 '<command>'
perf stat -r 10 -- <command>
perf record -g -- <command>
perf report
heaptrack <command>
valgrind --tool=callgrind <command>
valgrind --tool=cachegrind <command>
valgrind --tool=massif <command>
```

Use platform equivalents on macOS and Windows. For high-performance binaries, evaluate PGO, ThinLTO, BOLT, link order, allocator choice, and CPU-specific dispatch only after algorithm/data-structure choices are sound.

## Levers

- Algorithmic complexity and data structure layout.
- Allocation count and ownership model.
- Cache locality, false sharing, alignment, and SoA/AoS tradeoffs.
- Branch predictability and error-path separation.
- Syscall and lock contention.
- SIMD and vectorization.
- Compile-time computation and template instantiation cost.
- Binary size, icache pressure, and cold-start behavior.

## Opportunity Matrix

Only change candidates with score >= 2.0:

```text
score = impact * confidence / effort
```

| Candidate | Hotspot evidence | Impact | Confidence | Effort | Score | Decision |
|---|---|---:|---:|---:|---:|---|

Impact is not "I think this matters"; it comes from profile percentage, p95/p99 effect, allocation count, cache misses, syscall count, or benchmark deltas.

## C/C++ Hot-Path Review

Check these before changing code:

- New or repeated `malloc`, `free`, `new`, `delete`, `realloc`, `std::function`, `std::regex`, locale, stream I/O, exceptions, RTTI, or virtual dispatch in hot loops.
- Hidden copies from `std::string`, `std::vector`, `std::optional`, ranges, lambda captures, initializer lists, and pass-by-value APIs.
- Buffer growth policy, realloc invalidation, and overflow before size multiplication.
- `size_t`, `time_t`, pointer difference, endian, and unaligned access assumptions.
- Branch predictability, error-path interleaving, cold code layout, and log/format calls in hot paths.
- Lock contention, atomics order, false sharing, thread-local access cost, and cross-thread frees.
- Syscall count, short I/O, `fsync`, `mmap`, page faults, transparent huge pages, NUMA locality, and fork/copy-on-write effects.
- Compiler flags that change semantics: `-ffast-math`, `-fno-exceptions`, `-fno-rtti`, LTO, PGO, `-march=native`, visibility, and sanitizer-disabled builds.

## Optimized Backend Proof

For SIMD, target-specific, platform-specific, or backend-dispatched code:

1. Keep a scalar or portable reference path unless the project explicitly forbids unsupported platforms.
2. Differential-test optimized and reference paths on the same inputs.
3. Record dispatch conditions: CPU feature, compiler, OS, endianness, alignment, build flags.
4. Verify fallback behavior on unsupported targets or compile-only matrixes.
5. If claiming autovectorization or instruction selection, include compiler vectorization output or disassembly for the relevant loop.
6. Do not ship `-march=native`, target intrinsics, or backend exclusion unless packaging and fallback behavior are proven.
7. Do not use UB as an optimization contract. If an assumption is required, assert it in debug or encode it in types.

## Floating-Point And UB Cautions

- Treat `-ffast-math`, FMA contraction, reassociation, denormal handling, rounding mode, NaN, infinities, and signed zero as semantic changes unless the contract says otherwise.
- Record acceptable numeric tolerance, comparison method, and input distribution for floating-point benchmarks.
- Never use UB as an optimization contract: signed overflow, shift bounds, alignment, effective type, strict aliasing, invalid pointer provenance, out-of-bounds pointer arithmetic, uninitialized reads, and data races must be ruled out or fixed.
- Encode hot-path preconditions with types or debug assertions where practical, then verify with UBSan/ASan/MSan/TSan or the closest available gate.

## Isomorphism Proof

Before editing, write:

```text
Change:
- Hotspot:
- Single lever:
- Behavior oracle captured before edit:
- Inputs/corpus covered:
- Ordering and tie-breaks:
- Error/errno/exception semantics:
- Floating-point semantics and tolerance:
- UB/alignment/aliasing assumptions:
- RNG/hash/time behavior:
- Allocation ownership and lifetime:
- ABI/API/layout impact:
- Thread-safety and atomic ordering:
- Backend/SIMD fallback equivalence:
- Portability impact:
- Golden/regression checks:
- Benchmark before/after:
- Residual risk:
```

## Guardrails

- Do not optimize cold code.
- Do not use UB for speed.
- Do not add target-specific intrinsics without scalar fallback or dispatch.
- Do not change floating-point semantics without declaring it.
- Do not benchmark debug builds unless debug performance is the product.
- For real-time code, average latency is not enough. Capture worst-case or bounded-tail evidence.
- Do not combine unrelated performance levers in one commit.
- Do not hide semantic changes in performance patches.
- Do not accept faster output if the oracle changed, error handling changed, or ABI/API behavior changed.

## Report Format

```text
Performance claim:
- Workload:
- Baseline:
- Baseline command and commit:
- Environment:
- Build configuration:
- Profile hotspot:
- Opportunity score:
- Change:
- Correctness/isomorphism proof:
- Result:
- Before/after timing table:
- CPU/cache/allocation/syscall evidence:
- Noise controls:
- Tool versions:
- Inputs/corpus:
- Benchmark limitations:
- ABI/API impact:
- Portability impact:
- Residual risk:
```
