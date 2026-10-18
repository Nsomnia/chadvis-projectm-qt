# Code Transform

## Principle

[REFACTOR-ISOMORPHISM.md](REFACTOR-ISOMORPHISM.md) governs the one transform that promises *no* behavior change. This reference governs the three that change something on purpose: **port** moves the same behavior to a new compiler/standard/platform/arch/language, **modernize** rewrites the implementation to current idioms, and **re-architect** changes structure, layering, ownership, or data shape. A behavior-changing transform is not a license to change behavior *silently*: every delta is either zero (proven against an oracle) or named, documented, and gated as intentional. Untracked drift is the failure mode this reference exists to forbid.

No oracle means no transform. No named delta means the delta is a bug.

## Mode Selection

Pick the narrowest mode that fits, then run its loop. When two modes apply, split the work into separate commits, one mode each — never fold a port into a modernize, never hide a re-architecture inside a port.

| Use | When | Mandatory gate |
|---|---|---|
| `refactor` (see [REFACTOR-ISOMORPHISM.md](REFACTOR-ISOMORPHISM.md)) | behavior, ABI, layout, and artifacts must all stay identical | refactor isomorphism |
| `port` | same behavior must hold under a new compiler, standard library, OS, CPU/arch, target triple, OR a new language (C/C++ ↔ Rust) | differential oracle |
| `modernize` | the standard is raised, a deprecated API is replaced, or a `clang-tidy modernize-*` rewrite is applied, with behavior held | refactor isomorphism + ABI/API |
| `re-architect` | layering, data structure, or ownership model changes by design; behavior of the public surface is preserved | migration ledger + tests + ABI/API |

If you cannot name the mode, you cannot name the oracle, and you must stop. "Improve it" is not a mode.

Disambiguators when a request is fuzzy:

- "Make it build on arm64 / under MSVC / on musl" → `port`. The behavior contract is fixed; only the toolchain/target moves.
- "Rewrite this hot loop in Rust" or "replace this C parser with a Rust crate" → `port` (cross-language), behind a frozen C ABI seam, with a parity oracle.
- "Use `std::optional` / ranges / `make_unique` instead of the old idiom" → `modernize`. Behavior, ABI, and API are held.
- "Split this God object" / "swap the linked list for a slab" / "move ownership into the registry" → `re-architect`. Public behavior is held; structure changes.
- "Make it faster" → not this reference. Use the Optimization Card and `performance` profile; a transform that also changes speed records the speed delta as an intentional ledger row, but the transform gate is still the one for its mode.

## Transform Never Silently Changes Behavior

The cross-cutting rule for all three modes:

1. Capture the origin behavior as an executable oracle *before* the first edit.
2. Every observable difference from origin is either zero (oracle-proven) or an entry in a delta ledger with rationale, owner, and a test that pins the new behavior.
3. One lever per commit; each commit reverts cleanly.
4. The same oracle and gates run on origin and target with identical inputs.
5. A delta discovered after the fact — not in the ledger — is a regression, not a feature, until an owner reclassifies it with evidence.

## Port

Cross compiler / standard / platform / arch, or cross-language C/C++ ↔ Rust. Behavior must be identical on origin and target, or the difference is a documented intentional delta (endianness-normalized output, widened error code, dropped platform quirk) with its own test.

Executable loop:

1. **Baseline** on the origin: build/tests/sanitizer gates, public API/ABI snapshot, and the representative output corpus. Record origin compiler family, standard version, and target triple.
2. **Oracle**: stand up a *differential* oracle. Name the origin triple, the target triple, the run path (an emulator such as QEMU, or named hardware), and the corpus size. Feed the identical corpus through origin and target; outputs must match byte-for-byte or differ only by a ledgered intentional delta.
3. **One lever**: port one leaf first — the unit with the fewest dependents — then climb. Do not port a caller before its callees have a green oracle.
4. **Verify**: rerun the differential oracle on the same corpus; diff origin vs target; any unledgered delta blocks.

Cross-language C/C++ → Rust handoff: cross-link the sibling [`legacy-to-rust-porting`](../../legacy-to-rust-porting/SKILL.md) skill and follow its parity-oracle and primitive-replacement contracts. Port leaf-first behind a stable C ABI seam: the Rust side exposes `extern "C"` with the origin's symbol names, layout, and ownership transfer unchanged, so callers cannot tell which side of the seam they are on. Keep the differential oracle running across the seam for every ported unit; the C ABI stays frozen until the whole subtree is Rust.

Differential oracle construction. The oracle is not a bare `diff`; it is a named, reproducible substructure the checker can verify:

- `origin-triple:` the triple/toolchain the baseline corpus was produced on (e.g. `x86_64-unknown-linux-gnu, gcc-13, -O2`).
- `target-triple:` the triple/toolchain under test (e.g. `aarch64-unknown-linux-gnu, clang-18, -O2`).
- `emulator:` or `hardware:` the run path — at least one — e.g. `emulator: qemu-aarch64 8.2` or `hardware: rpi5 arm64`. "Compiled only, not executed" is not a port oracle; say so and keep the claim narrow.
- `corpus:` the input set size and provenance (e.g. `corpus: 4096 fuzz-derived inputs + 37 golden files`). A corpus too small to bind the behavior is not an oracle.

Port hazards, each an oracle axis, not an afterthought:

| Axis | What slips silently across a port |
|---|---|
| Integer width | `long`/`size_t`/`time_t`/pointer width on LP64 vs LLP64 vs ILP32; format-string and serialization widths |
| Endianness | byte-order of persisted/wire data; `htonl`-free round-trips that worked only on the origin endianness |
| Layout/alignment | struct padding, bitfield order, alignment-trap faults on strict-alignment arches |
| UB the origin "defined" | signed overflow, type punning, uninitialized reads the origin compiler happened to make benign |
| `char` signedness | plain `char` is **signed** on x86-64 (SysV) but **unsigned** on AArch64 (AAPCS64) and RISC-V; `char c=0xFF; c<0` flips between targets. Verified: x86 → `is_negative=1`, aarch64/riscv64 → `is_negative=0` |
| libc/STL divergence | glibc vs musl vs MSVC STL vs libc++: errno values, locale, `printf` rounding, `qsort` stability |
| Floating point | x87 80-bit vs SSE, FMA contraction, `-ffast-math` defaults, rounding mode |
| ABI seam | symbol names, calling convention, struct layout, and ownership transfer across the C boundary (cross-language ports) |

Concrete cross-arch oracle (no hardware needed — install the cross GCC + an emulator, then run the *identical* driver on each target):

```bash
# toolchains: gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu qemu-user-static
gcc                   -O2 -I. driver.c lib.c -o d_x86            # origin, native
aarch64-linux-gnu-gcc -static -O2 -I. driver.c lib.c -o d_a64    # target 1
riscv64-linux-gnu-gcc -static -O2 -I. driver.c lib.c -o d_rv     # target 2
while read -r f; do
  echo "$(basename "$f") $(./d_x86 "$f")"                 >> out_x86.txt
  echo "$(basename "$f") $(qemu-aarch64-static ./d_a64 "$f")" >> out_a64.txt
  echo "$(basename "$f") $(qemu-riscv64-static ./d_rv  "$f")" >> out_rv.txt
done < corpus.lst
diff out_x86.txt out_a64.txt && diff out_x86.txt out_rv.txt    # any delta blocks the port
```

`-static` makes the target binaries self-contained (no `-L <sysroot>`); for dynamic builds use `qemu-aarch64-static -L /usr/aarch64-linux-gnu`. QEMU-user is not silicon: it does not model the target's relaxed memory order, so a *concurrent* port still needs real hardware or a memory-model checker — say so in the residual-risk row.

## Modernize

Raise the C/C++ standard, replace a deprecated API, or apply a `clang-tidy modernize-*` rewrite. Behavior, ABI, and API are held; the implementation changes. No batch "modernize everything" commit — each transform is its own row and its own commit.

Executable loop:

1. **Baseline**: build warning-clean, tests, ABI/API snapshot, and the standard version in force.
2. **Oracle**: per transform, a refactor-isomorphism proof (the exact `clang-tidy` check name, e.g. `modernize-use-nullptr`, `modernize-loop-convert`, `modernize-make-unique`, or the manual rewrite description) *plus* the behavior axes it touches, *plus* an ABI/API check. A `clang-tidy` suggestion is a candidate, never a justification.
3. **One lever**: one transform per commit. Do not let a `use-nullptr` sweep also reorder includes or change a signature.
4. **Verify**: tests + sanitizer for the touched axis, ABI/API snapshot unchanged (or the break is a deliberate non-modernize change, split out), warning count not worse.

Each modernize row records: the transform (check name or manual rewrite), the behavior proof (the isomorphism axes checked), and the ABI/API result. A standard-version bump that changes overload resolution, ODR, `constexpr` evaluation, or implicit conversions is a behavior axis — prove it, do not assume the new standard is a superset.

Per-transform isomorphism rows (each its own commit, never batched):

| Transform | Behavior the row must prove is unchanged |
|---|---|
| `modernize-use-nullptr` | no overload-resolution shift where `0`/`NULL` selected an integer overload |
| `modernize-loop-convert` (range-for) | iteration order, container mutation safety, iterator invalidation, copy-vs-reference of the element |
| `modernize-make-unique`/`make-shared` | allocation count/order, exception-safety guarantee, custom-deleter and allocator behavior |
| `modernize-use-override`/`use-default` | vtable layout and ABI unchanged; defaulted special members preserve copy/move/destructor timing |
| deprecated-API replacement (e.g. `std::auto_ptr`→`unique_ptr`, `gets`→`fgets`) | ownership/move semantics, truncation/error semantics, return-code contract |
| standard-version raise (C++17→20/23) | overload set, ODR, `constexpr`/`consteval` evaluation, implicit-conversion and `<=>` rewrite behavior |

## Re-architect

Bounded structural change: new layering, a new data structure, or a new ownership model, with the public surface's behavior preserved. Reversible. One structural lever per commit. Forbidden: a "rewrite" that lands as one giant diff with no per-step oracle.

Executable loop:

1. **Baseline**: tests green, ABI/API snapshot, and the caller census (every consumer of the surface being restructured).
2. **Oracle**: a migration ledger. Each row is one structural step with a per-commit caller census (which callers this step touches and how they are kept compiling/passing), the tests proving the step, and an ABI/API snapshot delta (zero, or a ledgered intentional break). The ledger makes the migration reversible: each step names its revert.
3. **One lever**: one structural change per commit (extract a layer; swap one data structure; move one ownership boundary). Tests pass at every step, not just at the end.
4. **Verify**: tests at each step, ABI/API snapshot per step, and a final census proving no caller was orphaned.

Migration ledger columns (one row per structural step, in commit order):

| Step | Structural lever | Caller census (this step) | Tests proving the step | ABI/API delta | Revert |
|---|---|---|---|---|---|

The `caller-census:` field names which consumers each step touches and how they are kept green; the `ledger:` field is the ordered, reversible step list. A step with no revert is not part of a bounded re-architecture — it is a one-way rewrite, which this mode forbids.

Re-architect hazards: an old package/class name becoming the new module boundary (it should not), business logic migrating into adapters or a different layer, ownership moving from RAII to manual or vice versa, and a data-structure swap that changes iteration order, complexity class, or invalidation semantics. Each is a behavior axis even when the public signature is unchanged.

## Stop Conditions

Stop and do not commit when any holds:

- The mode is unnamed, or two modes are folded into one commit.
- `port`: no differential oracle, or the oracle does not name origin triple, target triple, run path (emulator or hardware), and corpus size.
- `port`: an output delta exists that is not zero and not a ledgered intentional delta.
- Cross-language port crosses a C ABI seam whose symbols, layout, or ownership transfer changed without a ledger row.
- `modernize`: a transform lacks its per-transform isomorphism row (check name or manual rewrite + behavior proof) or its ABI/API check.
- `modernize`: a single commit batches more than one transform, or mixes a transform with formatting/behavior change.
- `re-architect`: no migration ledger, no per-commit caller census, or tests are red at any intermediate step.
- `re-architect`: a step is not reversible, or more than one structural lever lands in one commit.
- Any mode: a delta was discovered after the fact and not yet reclassified by an owner with evidence.

## Anti-Patterns

| Anti-pattern | Why it fails | Stop signal |
|---|---|---|
| "Big-bang rewrite" | one diff with no per-step oracle hides every regression in the noise | `re-architect` with no migration ledger or no per-commit caller census |
| Port that only compiles | "builds on arm64" is not "behaves on arm64"; UB and width bugs surface only at runtime | `port` oracle with no `emulator:`/`hardware:` run path |
| Corpus theater | a 3-input "corpus" cannot bind real behavior across a triple change | `corpus:` too small to exercise the touched paths |
| `clang-tidy` as justification | the tool suggests candidates, not proofs; a `modernize-*` hit is a lead | `modernize` row with no behavior axes and no ABI/API check |
| Folded modes | a port that also modernizes, or a re-architecture smuggled inside a port | more than one mode in one commit |
| Silent intentional delta | a "known difference" with no ledger row, owner, or pinning test | output differs and the diff is waved off as expected |
| Cross-language seam drift | the Rust replacement changes the C symbol/layout/ownership "because Rust is nicer" | C ABI seam changed without a ledger row while callers still expect the origin contract |
| Caller orphan | a structural step compiles the changed layer but leaves a consumer broken | re-architect step with an incomplete caller census |

## Evidence

Fill the matching gate row in the gate report, then run `cpp_evidence_check.py` with the profile and `--require-transform-proof`. When the gate is `passed`, the checker strict-checks the named fields below.

| Mode | Profile | Gate(s) | Evidence fields the gate must name |
|---|---|---|---|
| `port` | `port` | `differential oracle` | `origin-triple:`, `target-triple:`, at least one of `emulator:`/`hardware:`, `corpus:` |
| `modernize` | `modernize` | `refactor isomorphism` + `ABI/API` | per-transform check name or manual rewrite, behavior axes, ABI/API result (existing isomorphism + ABI fields) |
| `re-architect` | `rearchitect` | `migration ledger` + `tests` + `ABI/API` | `caller-census:`, `ledger:`, plus passing `tests` and an `ABI/API` snapshot |

```text
Transform claim:
- Mode:                 port | modernize | re-architect
- Origin:               compiler/std/triple/language
- Target:               compiler/std/triple/language
- Oracle:               differential oracle | per-transform isomorphism | migration ledger
- origin-triple:        (port)
- target-triple:        (port)
- emulator:/hardware:   (port — at least one)
- corpus:               (port — size)
- caller-census:        (re-architect — per-commit)
- ledger:               (re-architect — reversible per-step rows)
- ABI/API result:       (modernize, re-architect)
- Intentional deltas:   each with rationale, owner, pinning test
- One lever:
- Stop condition:
- Residual risk:
```
