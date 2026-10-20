# Memory Safety

## Principle

C/C++ can be excellent only when ownership, bounds, lifetimes, initialization, aliasing, and error paths are explicit. Tests are evidence, not proof. Combine API design, static checks, dynamic checks, fuzzing, and manual invariant review.

## C++ Defaults

- Use RAII for every resource: memory, file descriptors, sockets, handles, locks, mapped memory, GPU resources.
- Prefer value types, `std::unique_ptr`, `std::shared_ptr` only for real shared ownership, `std::span`, `std::string_view` with clear lifetime constraints, and containers over raw arrays.
- Use `gsl::not_null`, `gsl::owner`, and `gsl::span` when the project already has GSL or accepts it.
- Avoid owning raw pointers. If unavoidable at an ABI boundary, wrap immediately and document ownership transfer.
- Avoid `reinterpret_cast`, `const_cast`, C-style casts, type punning through unions, and placement-new lifetime tricks unless the invariant is documented beside the code.
- Treat `std::string_view`, iterator, and `std::span` invalidation as lifetime hazards. They deserve tests and review, not casual convenience.
- Do not throw across C ABI, plugin, thread, signal, or foreign-language boundaries.

## C Defaults

- Put ownership in names and types: `init/free`, `create/destroy`, `borrow`, `take`, `release`, `span` structs with pointer plus length.
- Use one cleanup path per function for multi-resource functions.
- Prefer size-carrying structures over naked pointer/length pairs when they travel together.
- Avoid unbounded string APIs. Prefer bounded operations with explicit truncation/error semantics.
- Initialize all storage before use. Treat padding and partial initialization as security-sensitive when serializing or hashing.
- Use `restrict` only when aliasing has been proven and documented.
- Keep allocator ownership boundaries explicit. Do not free memory with a different allocator family than the allocator that produced it.

## Undefined Behavior Review

Scan for:

- Out-of-bounds access, pointer arithmetic outside arrays, invalid iterators.
- Signed overflow, invalid shifts, divide by zero.
- Use-after-free, use-after-scope, double free, invalid free.
- Null, misaligned, or incorrectly typed dereference.
- Uninitialized reads, including padding and partially initialized structs.
- Data races and invalid atomics.
- Strict-aliasing violations and object-lifetime violations.
- Format string mismatches and varargs type mismatches.
- `longjmp`, signal handlers, and exception boundaries that skip cleanup.

## Mechanical Gates

Use as applicable:

```text
ASan+UBSan: -fsanitize=address,undefined -fno-omit-frame-pointer -g
TSan:       -fsanitize=thread -g -O1
MSan:       -fsanitize=memory -fsanitize-memory-track-origins -g
```

Add Clang Static Analyzer, `clang-tidy`, and project-approved commercial analyzers for security-sensitive changes.

## Current Clang Safety Features To Know

- `-Wunsafe-buffer-usage` and C++ Safe Buffers help migrate raw buffer operations toward bounds-carrying containers/views.
- Clang Lifetime Safety Analysis warns about dangling pointer/reference/view defects. It is compile-time analysis and currently has limitations.
- `-fbounds-safety` is a C extension design for bounds annotations and checks. As of the referenced Clang documentation, it is not generally available to users, so treat it as roadmap material unless the target toolchain supports it.

## Escape Hatch Format

When unsafe or non-portable code remains, write the invariant before finalizing:

```text
Unsafe contract:
- Why this is needed:
- Valid inputs:
- Ownership/lifetime:
- Bounds/alignment:
- Aliasing:
- Threading:
- Platform/ABI:
- How it is checked:
- Tests/sanitizers/fuzz evidence:
```
