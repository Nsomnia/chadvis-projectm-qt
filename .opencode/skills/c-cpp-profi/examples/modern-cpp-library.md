# Example: Modern C++ Library Change

Use for C++ libraries with templates, concepts, RAII types, public headers, exceptions, or package exports.

## Starting Point

```text
Task: refactor a public C++ type or add a template overload.
Boundary: headers, exported symbols, compile-time diagnostics, ABI if not header-only.
Primary risks: overload resolution, ODR, layout, vtables, copy/move behavior, exception safety.
```

## Required Skill Path

1. Read `REFACTOR-ISOMORPHISM.md`, `BUILD-PORTABILITY.md`, and `QUALITY-GATES.md`.
2. Capture baseline:

```bash
cmake --preset <configure-preset>
cmake --build --preset <build-preset>
ctest --preset <test-preset> --output-on-failure
clang-tidy -p <build-dir> <changed-files>
```

3. For public headers, compile at least one downstream consumer or installation smoke.
4. For ABI-bearing libraries, compare exported symbols and layout-sensitive APIs.
5. For templates/concepts, record changed overload set, explicit instantiations, and compile errors that must remain compile errors.

## Evidence Packet

```text
Modern C++ evidence:
- Public headers:
- Template/concept/overload impact:
- ABI/layout/vtable/mangling impact:
- RAII/destructor/copy/move behavior:
- Exception-safety guarantee:
- Downstream compile:
- Tests/static/sanitizer gates:
- Residual risk:
```

## Refusal Conditions

- Public template/concept changes without callsite and compile-error census.
- "Rule of 0" migration without copy/move/destructor proof.
- Header-only refactor that ignores ODR, include order, or build-time impact.
