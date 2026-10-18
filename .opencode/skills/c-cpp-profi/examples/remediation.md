# Example: Worked Remediation (crash fix + binary-size reduction)

Two threads on a small TLV record parser (`src/tlv.c`, built into `tlvtool`).
Thread 1 fixes a heap overflow by instantiating
[REMEDIATION-RECIPES.md](../references/REMEDIATION-RECIPES.md) Recipe 1; Thread 2
applies the Part B no-size-regression method. One commit per thread.

## Thread 1 — Fix a crash (Recipe 1: unchecked alloc-multiply)

Minimized reproducer (`corpus/crash-mul`): a record header claiming
`count = 0x20000001`, `elem = 0x80` so `count * elem` wraps a 32-bit `size_t`.

```text
==1==ERROR: AddressSanitizer: heap-buffer-overflow WRITE of size 256
    #0 tlv_read_array src/tlv.c:48  (after malloc(count * elem) returned a tiny buffer)
SUMMARY: AddressSanitizer: heap-buffer-overflow src/tlv.c:48
```

```c
/* Before (src/tlv.c:46): count * elem wraps; tiny alloc, then the loop overruns it. */
void *buf = malloc(count * elem);
for (uint32_t i = 0; i < count; i++) read_elem(buf, i, elem);
```

```c
/* After: overflow-checked allocation; fail closed before the multiply lands. */
size_t bytes;
if (__builtin_mul_overflow(count, elem, &bytes)) { errno = ENOMEM; return -1; }
void *buf = malloc(bytes);
for (uint32_t i = 0; i < count; i++) read_elem(buf, i, elem);
```

Regression test: `corpus/crash-mul` is promoted into the parser corpus and
`tests/test_tlv.c` asserts `tlv_read_array` returns `-1` with `errno == ENOMEM`
on it. Invariant restored: the byte count handed to `malloc` equals the
mathematical product, or the path fails before allocating — no truncated size
reaches the write loop.

## Thread 2 — Reduce binary size (gc-sections lever)

One lever, behavior-preserving: add `-ffunction-sections -fdata-sections` to the
compile and `-Wl,--gc-sections` to the link so unreferenced helpers are dropped.
No source change, so the behavior oracle (golden parse outputs + the test suite)
must be byte-identical.

```text
Size delta:
- Binary/section: tlvtool, .text + total
- Build: gcc-13, -O2, no LTO, --strip-all, x86_64-linux-gnu
- Before bytes:  size -A => .text 41232; bloaty total 78064
- After bytes:   size -A => .text 33980; bloaty total 70112
- Delta: .text -7252 B (-17.6%), total -7952 B (-10.2%)
- One lever: -ffunction-sections -fdata-sections + -Wl,--gc-sections
- Justification: pure dead-code GC of unreferenced TU-local helpers; nothing grew.
- Behavior oracle: 41 golden parse outputs + tests byte-identical before/after.
```

## Evidence Packet

```text
# C/C++ Gate Report

## Change Scope
- Issue/task: fix tlv_read_array heap overflow (Recipe 1) + shrink tlvtool via gc-sections
- Touched files: src/tlv.c, tests/test_tlv.c, corpus/crash-mul, build flags (no source change for size)
- Public API/ABI touched: no
- User-visible rendering/artifacts touched: no
- Parser/input/security boundary touched: yes (TLV record parser, attacker-supplied count/elem)
- Threads/locks/atomics/signals touched: no
- Refactor/simplification claim: no
- Performance claim: yes (binary-size reduction, no-size-regression gate)

## Commands
| Gate | Status | Command | Evidence |
|---|---|---|---|
| inventory | passed | bash scripts/cpp_inventory.sh . | build: make; std: c11; target tlvtool; 1 fuzz harness |
| compile | passed | make CFLAGS='-O2 -Wall -Wextra' | warning-clean: yes |
| tests | passed | make test | 41 tests pass; new corpus/crash-mul case asserts errno==ENOMEM, rc==-1 |
| static analysis | passed | clang-tidy -checks=bugprone-* src/tlv.c | findings: 0 after the fix; pre-fix flagged the unchecked multiply |
| ASan+UBSan | passed | make asan && ./tlvtool < corpus/crash-mul | ASan+UBSan clean; pre-fix heap-buffer-overflow at src/tlv.c:48 no longer reproduces; unsigned-integer-overflow not triggered |
| fuzz/corpus | passed | ./tlv_fuzzer -runs=5000000 corpus/ (ASan+UBSan) | pre-fix: crash at src/tlv.c:48 in 3s; post-fix: 0 crashes over 5e6 execs; crash-mul minimized and promoted to corpus/ as a permanent regression |
| performance | passed | size -A tlvtool; bloaty -d symbols old.elf -- tlvtool | baseline: .text 41232 / total 78064; profile: bloaty per-symbol size oracle; score: -7952 B total (-10.2%) reclaimed dead helpers; oracle: 41 golden parse outputs + tests byte-identical; after: .text 33980 / total 70112; result: behavior-preserving size win, no hot-path regression |

## Residual Risk
- Missing gates: fuzz/corpus extended soak (only the minimized seed + existing corpus replayed) and TSan (no threads touched).
- Why missing gates are acceptable or follow-up issue: the overflow class is closed by the checked multiply and a regression seed; a longer fuzz soak is incremental coverage, not a correctness gap.
- Follow-up issues: run a 30-minute libFuzzer soak on tlv_read_array before the next release tag.
```

Verify the packet (memory closes the crash, performance carries the size delta):

```bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py <this-packet>.md \
  --profile memory --profile performance --require-performance-proof
```

## Refusal Conditions

- A crash "fix" with no minimized reproducer promoted into the corpus/regression suite.
- A size delta claimed without a baseline, a single named lever, and a behavior oracle proving the smaller binary is isomorphic.
- `-fno-exceptions`/`-fno-rtti` sold as a free size win — those change semantics and belong under [REFACTOR-ISOMORPHISM.md](../references/REFACTOR-ISOMORPHISM.md).
