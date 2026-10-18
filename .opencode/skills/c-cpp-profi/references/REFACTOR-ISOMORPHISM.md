# Refactor Isomorphism

## Principle

In C/C++, "no behavior change" includes ABI, API, layout, vtables, ownership, allocator boundaries, initialization order, exception guarantees, compile-time behavior, generated artifacts, and performance-sensitive ordering. If any of those changes, it is not a pure refactor.

No proof means no refactor.

## Required Loop

1. Baseline before editing: build/tests, goldens or representative outputs, sanitizer/static gates for the touched risk, public API/ABI snapshot, LOC/duplication snapshot, and compile warnings.
2. Map all sites: callers, overrides, exported headers, generated config, CMake/Meson targets, package consumers, docs/examples, and tests.
3. Score the candidate: `score = LOC_saved * confidence / risk`. Only implement score >= 2.0.
4. Write the isomorphism card before editing.
5. Change one lever per commit.
6. Verify with the same inputs and gates after editing.
7. Record rejected candidates where similarity was accidental or proof was weak.

Do not accept "cleaner", "modern", "tool suggested", or "AI suggested" as rationale. The change must remove real complexity with evidence.

## Census Before Edit

Record:

- Duplicate spans: exact, parametric, gapped, semantic, or accidental rhyme.
- All callers, public headers, exported symbols, tests, examples, docs, manpages, bindings, plugins, FFI users, and downstream package surfaces touched.
- Build graph impact: CMake/Meson/autotools targets, feature options, compile definitions, generated config headers, install/export rules.
- ABI/API impact: symbols, struct layout, enum width, vtables, name mangling, calling convention, inline functions, public templates, allocator ownership, and STL types crossing library boundaries.
- Runtime axes: output bytes, ordering, error/errno/exception semantics, logs, metrics, files, syscalls, allocation behavior, thread behavior, timing, and resource lifetime.

## Opportunity Matrix

Use `complexity_removed`, not just raw LOC, when a refactor removes a recurring unsafe idiom or overloaded abstraction.

```text
score = (complexity_removed * confidence) / risk
```

| Dimension | 5 | 3 | 1 |
|---|---|---|---|
| Complexity removed | duplicated subsystem or recurring unsafe idiom | 20-50 LOC or one medium abstraction | cosmetic spelling/local cleanup |
| Confidence | scanner, census, goldens, tests, and symbol checks agree | tests cover main paths but ABI/caller coverage is partial | visual similarity only |
| Risk | public ABI/API, ownership, concurrency, parser/input, allocator, templates, or build config | module boundary or shared state | private pure helper |

## Duplication Types

| Type | Shape | C/C++ decision |
|---|---|---|
| I exact clone | byte-identical logic | extract if callsites have same contract |
| II parametric clone | same shape, different constants/types | parameterize if variance is one clear axis |
| III gapped clone | same skeleton with small differences | use strategy/enum only if differences are bounded |
| IV semantic clone | different code with same apparent behavior | require property/golden proof before merging |
| V accidental rhyme | looks similar but evolves separately | leave separate |

Rule of 3: two callsites are a note; three callsites can justify abstraction. Do not climb from copy-paste directly to traits, class hierarchies, templates, or macros without evidence that the open abstraction is needed.

## C/C++ Isomorphism Axes

Cover every applicable axis:

- Output bytes, return values, ordering, tie-breaking, error codes, `errno`, exceptions, panics/assertions, logs, metrics, trace spans, syscalls, files, network messages, and golden artifacts.
- Public API: exported headers, symbols, visibility, names, overload sets, ADL, concepts/SFINAE participation, implicit conversions, default arguments, macros, feature-test macros, and docs/examples.
- ABI: struct/class layout, padding, alignment, bitfields, enum width, `offsetof` assumptions, packing pragmas, calling convention, vtable order, RTTI, name mangling, inline namespace, symbol version, exception boundary, allocator ownership, and C vs C++ linkage.
- RAII and ownership: constructor/destructor order, copy/move constructors, assignment, Rule of 0/3/5, moved-from state, pointer/view lifetimes, custom deleters, allocator family, and cleanup labels.
- Exception safety: nothrow/basic/strong guarantee, rollback behavior, destructor `noexcept`, and FFI/thread/signal exception boundaries.
- Templates/build: instantiation set, overload resolution, concept/SFINAE match set, ODR, inline functions, explicit instantiations, compile-time diagnostics, compile-time cost, generated headers, target properties, link order, and install/export metadata.
- Memory/UB: aliasing, alignment, pointer provenance, signed overflow, lifetime extension, iterator invalidation, `string_view`/`span` dangling, and sanitizer behavior.
- Concurrency: lock order, atomics, condition variables, thread lifecycle, callback reentrancy, and happens-before edges.
- Performance: laziness, allocation count, short-circuiting, branch order, cache layout, binary size, and benchmark result when the code is hot.
- Portability: endian, word size, `time_t`, path encoding, platform branches, compiler extensions, and standard version.

## Isomorphism Card

Write this before editing:

```text
Refactor claim:
- Scope:
- Candidate:
- Clone type:
- Expected LOC saved:
- Score:
- Baseline commit:
- Baseline commands:
- One lever:
- Callsite census:
- Public API impact:
- ABI/layout/vtable/mangling impact:
- Ownership/allocator impact:
- RAII/destructor/copy/move impact:
- Error/errno/exception semantics:
- Ordering/tie-breaking/side effects:
- Floating-point/RNG/hash/time behavior:
- Laziness/allocation/performance behavior:
- Template/concept/ODR/compile-time behavior:
- Macro/preprocessor/build-system behavior:
- Concurrency behavior:
- Golden/artifact behavior:
- Tests/sanitizers/static analysis:
- Before/after LOC and warning counts:
- Rejected candidates:
- Deferred findings and bead ids:
- Stop condition:
- Residual risk:
```

## C Refactor Hazards

- Replacing pointer+length with a struct changes ABI unless the boundary is private.
- Reordering struct fields can change padding, binary protocol, file format, or FFI layout.
- Inlining helpers can change `errno` preservation, cleanup order, or `goto`-based unwinding.
- Converting macros to functions can change type-generic behavior, single-evaluation behavior, constant-expression use, or `#ifdef` behavior.
- Converting functions to macros can change evaluation count and debug symbol behavior.
- Moving static storage can change initialization, thread-safety, and linker visibility.
- "Safer" string APIs can change truncation and error semantics.

## C++ Refactor Hazards

- Raw pointer to `unique_ptr` changes ownership, call signatures, and ABI.
- `shared_ptr` can hide cycles and change destruction timing.
- Rule-of-0 migrations must preserve copy/move behavior and destructor timing.
- Class hierarchy to `std::variant` closes the extension set and changes storage, RTTI, and ABI.
- Concepts replacing SFINAE can change overload resolution and compile errors.
- `constexpr` promotion can change ODR, initialization order, and diagnostic timing.
- Ranges can change laziness, iterator category, lifetime, and allocation.
- `std::string_view` and `std::span` refactors can create dangling views.
- Pimpl can stabilize ABI but changes allocation, nullability, and exception behavior.

## Build-System Refactors

For CMake/Meson/Make refactors:

- Capture configure, build, test, install, and downstream-consumer baseline.
- Preserve target names, include dirs, compile definitions, link libraries, visibility, RPATH/install names, pkg-config/CMake exports, and generated headers.
- Compare `compile_commands.json` for changed flags and source lists.
- Treat link order and transitive dependencies as behavior.
- Do not remove build targets, examples, tests, or install artifacts without explicit permission.

## Evidence And Stop Conditions

Stop only when:

- Baseline was captured before editing.
- One lever was applied.
- Baseline and after gates ran on the same inputs.
- The callsite census is complete.
- ABI/API/layout is unchanged or the intentional break is documented as non-refactor work.
- Outputs/goldens/artifacts match or differences are explicitly intentional in a separate change.
- Sanitizers/static analysis do not reveal a new risk.
- No new warnings or build portability regressions appeared.
- Complexity or LOC moved in the intended direction without adding worse coupling.
- Rejected candidate list explains every tempting but unsafe merge.
- Deferred work is tracked with exact risk.

## Anti-Patterns

- Hiding a bug fix in a refactor commit.
- Combining formatting, behavior, performance, and simplification in one diff.
- Merging similar-looking code without checking divergent error handling.
- Replacing C macros blindly with functions or C++ templates blindly with concepts.
- Deleting files, tests, examples, or targets without explicit permission.
- Calling ABI-breaking changes "internal" because current tests pass.
- Creating a general abstraction for two callsites.
- Accepting "cleaner" when goldens, symbols, or callsites were not checked.
