# Example: Code Transform (port + modernize + re-architect)

Use when a request changes the compiler/standard/platform/arch/language (`port`),
rewrites to current idioms (`modernize`), or changes structure/ownership
(`re-architect`). One mode per commit, each with its own oracle. See
[CODE-TRANSFORM.md](../references/CODE-TRANSFORM.md) for the loops and stop conditions.

## Starting Point

```text
Repo: a small C config library (src/cfg.c, include/cfg.h) shipped as libcfg.so.
Boundary: public C ABI used by 3 callers; serialized config blob persisted on disk.
Primary risks: integer width + endianness across arches, NULL idioms, a global parser singleton.
```

## Modernize transform (one commit)

- Lever: apply `clang-tidy modernize-use-nullptr` to `src/cfg.c` (raise the TU to C++17 compile path used by tests; the C ABI is unchanged).
- Isomorphism: no overload-resolution shift — every rewritten `NULL`→`nullptr` site was a pointer context, none fed an integer overload; iteration order and error/return codes unchanged.
- ABI/API: `nm -D --defined-only` symbol set and `cfg_t` layout identical before/after; `abidiff` clean.

## Port transform (separate commit)

- Lever: port the leaf serializer `cfg_pack()` from `x86_64-linux-gnu` to `aarch64-linux-gnu`; climb to callers only after the leaf oracle is green.
- Differential oracle: feed the identical corpus through origin and target; outputs must match byte-for-byte. Endianness of the persisted blob is normalized to little-endian as a ledgered intentional delta with its own pinning test.
- Run path: QEMU user emulation (no aarch64 hardware in CI); claim stays narrow to the emulated path.

## Re-architect transform (separate commit)

- Lever: replace the global parser singleton `g_cfg_ctx` with an injected `cfg_ctx*` threaded through the public entry points; public behavior preserved.
- Migration ledger: one reversible row — `extract-ctx` (revert: reinstate `g_cfg_ctx` shim) — with a per-commit caller census of all 3 consumers kept compiling and green.
- ABI/API: new `cfg_ctx*` parameter is additive behind a back-compat inline shim, so the exported symbol set is unchanged; tests green at the step, not just at the end.

## Evidence Packet

```text
# C/C++ Gate Report

## Change Scope
- Issue/task: transform libcfg — modernize NULL→nullptr, port serializer to aarch64, inject parser context
- Touched files: src/cfg.c, include/cfg.h
- Public API/ABI touched: yes (additive shim only)
- User-visible rendering/artifacts touched: no
- Parser/input/security boundary touched: no
- Threads/locks/atomics/signals touched: no
- Refactor/simplification claim: no (re-architect mode, structural change tracked by ledger)
- Performance claim: no

## Commands
| Gate | Status | Command | Evidence |
|---|---|---|---|
| inventory | passed | bash scripts/cpp_inventory.sh . | build: meson; std: c11 (+c++17 test path); 3 downstream callers |
| compile | passed | meson compile -C build | warning-clean: yes |
| tests | passed | meson test -C build | 214 tests pass at every migration step, not only at the end |
| refactor isomorphism | passed | clang-tidy -checks=modernize-use-nullptr src/cfg.c | check: modernize-use-nullptr; axes: no overload-resolution shift (all sites pointer context), iteration order + return codes unchanged; ABI/API: layout identical |
| differential oracle | passed | qemu-aarch64 ./oracle_diff corpus/ | origin-triple: x86_64-unknown-linux-gnu, gcc-13, -O2; target-triple: aarch64-unknown-linux-gnu, clang-18, -O2; emulator: qemu-aarch64 8.2; corpus: 4096 fuzz-derived inputs + 37 golden files; byte-equal except 1 ledgered endianness-normalization delta |
| migration ledger | passed | git log --oneline transform/extract-ctx | caller-census: 3 consumers (cli, daemon, test-shim) compiled+green this step; ledger: 1 reversible row extract-ctx (revert: reinstate g_cfg_ctx shim) |
| ABI/API | passed | bash scripts/cpp_abi_snapshot.sh build/libcfg.so /tmp/libcfg.before.so | nm -D symbol set identical; cfg_t layout identical; abidiff clean; cfg_ctx* added behind back-compat inline shim |

## Residual Risk
- Missing gates: aarch64 hardware run (only QEMU exercised) and TSan (no threads touched).
- Why missing gates are acceptable or follow-up issue: port claim is scoped to the emulated path; hardware soak tracked as follow-up.
- Follow-up issues: run cfg_pack differential oracle on rpi5 arm64 hardware before claiming bare-metal parity.
```

Run this to verify all three modes at once:

```bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py <this-packet>.md \
  --profile port --profile modernize --profile rearchitect --require-transform-proof
```

### C/C++ → Rust handoff

When the port crosses into Rust, follow the sibling
[`legacy-to-rust-porting`](../../legacy-to-rust-porting/SKILL.md) skill: reuse the
differential oracle as the parity oracle across the seam, expose the Rust side as
`extern "C"` with the origin's symbol names, layout, and ownership transfer frozen,
and port leaf-first (callees before callers) so consumers cannot tell which side of
the frozen C ABI seam they are on.

## Refusal Conditions

- A `port` oracle that names no `emulator:`/`hardware:` run path ("compiles on aarch64" is not "behaves on aarch64").
- A `modernize` row with a `clang-tidy` hit but no behavior axes and no ABI/API check.
- A `re-architect` step with no revert, or a caller census that leaves any of the 3 consumers orphaned.
- Two modes folded into one commit (a port that also modernizes, or a re-architecture hidden inside a port).
