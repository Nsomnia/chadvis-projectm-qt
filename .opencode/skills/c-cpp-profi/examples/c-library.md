# Example: C Library Change

Use for C libraries with public headers, shared/static artifacts, FFI consumers, or package exports.

## Starting Point

```text
Task: change behavior in src/foo.c and include/foo.h.
Boundary: public C API and ABI.
Primary risks: ownership, errno/status semantics, struct layout, symbol visibility, allocator boundary.
```

## Required Skill Path

1. Read `QUALITY-GATES.md`, `MEMORY-SAFETY.md`, and `BUILD-PORTABILITY.md`.
2. Inventory build/test shape with `cpp_inventory.sh`.
3. Capture public API/ABI baseline before editing:

```bash
nm -D --defined-only build/libfoo.so > /tmp/foo.symbols.before
cc -Iinclude examples/smoke.c -Lbuild -lfoo -o /tmp/foo-smoke-before
/tmp/foo-smoke-before
```

4. Make the narrow change.
5. Rebuild and rerun focused tests, static analysis, sanitizer gate, and downstream smoke.

## Evidence Packet

```text
C library evidence:
- Public header impact:
- ABI/symbol impact:
- Struct/enum/layout impact:
- Ownership and allocator boundary:
- errno/status semantics:
- Tests:
- Static analysis:
- ASan+UBSan or Valgrind:
- Downstream consumer smoke:
- Residual risk:
```

## Refusal Conditions

- No symbol/header/layout baseline for a public API change.
- Changed allocator ownership without updating docs/examples.
- Replaced C macros/functions without proving evaluation and constant-expression behavior.
