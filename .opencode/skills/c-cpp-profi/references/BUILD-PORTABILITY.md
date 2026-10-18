# Build, ABI, And Portability

## Build System Discipline

- Preserve the existing build system and presets.
- Do not create a parallel build system unless requested.
- Keep generated files out of hand edits.
- Export `compile_commands.json` when using Clang tooling.
- Prefer target-based CMake over global flags when editing CMake.
- Keep local developer settings out of committed presets.
- For sanitizer/fuzz scaffolds, read [TOOLCHAIN-TEMPLATES.md](TOOLCHAIN-TEMPLATES.md) and adapt the assets instead of inventing flags from memory.

## Standards

If unspecified, treat ISO/IEC 9899:2024 as the current C standard and ISO/IEC 14882:2024 as the current published C++ standard. Project constraints may require older standards. Document any feature that raises the required standard.

## Portability Checklist

- Compiler: Clang, GCC, MSVC, embedded vendor compiler.
- OS: Linux, macOS, Windows, BSD, RTOS, bare metal.
- CPU: x86_64, arm64, 32-bit, endian assumptions, alignment traps.
- Libc/STL: glibc, musl, MSVC STL, libc++, libstdc++, embedded libraries.
- Exceptions, RTTI, threads, dynamic allocation, locale, filesystem availability.
- Warning and sanitizer support differences.

## ABI/API Checklist

For public libraries:

- C ABI or C++ ABI boundary?
- Symbol visibility and export map.
- Struct/class layout, padding, alignment, enum size.
- Exception crossing boundary.
- Allocator crossing boundary.
- Ownership transfer and lifetime of returned pointers/views.
- Thread-safety guarantees.
- Versioning and deprecation policy.

## ABI Workflow

Use this when the change touches a public header, exported symbol, shared library, plugin boundary, FFI surface, SDK package, or persisted binary format.

1. Define the supported contract: source API, binary ABI, C ABI, C++ ABI, wire/file format, or downstream plugin compatibility.
2. Build the old and new artifacts with the same compiler family, target triple, standard library, visibility flags, build type, and feature flags.
3. Capture exported symbols:

```bash
nm -D --defined-only <lib.so>
readelf -Ws <lib.so>
objdump -T <lib.so>
dumpbin /EXPORTS <lib.dll>
```

Use the platform-appropriate subset. Prefer LLVM equivalents such as `llvm-nm` or `llvm-readelf` when that is the project toolchain.

4. Compare ABI with available tooling:

```bash
abidiff <old.so> <new.so>
abi-dumper <old.so> -o old.abi
abi-dumper <new.so> -o new.abi
abi-compliance-checker -l <name> -old old.abi -new new.abi
pahole -F dwarf -C <public_type> <lib.so>
```

When using `abi-dumper -public-headers`, first verify that `ctags --version` is Universal Ctags. Exuberant Ctags can misparse headers and produce dumps that `abi-compliance-checker` rejects. If public-header filtering is unavailable, say so and run an unfiltered comparison plus explicit symbol and layout checks; do not present it as a clean public-API-only report.

On Debian/Ubuntu-style systems, `abidiff` is commonly shipped by the `abigail-tools` package. If it is unavailable, record that exact package/tool gap and use `abi-dumper`, `abi-compliance-checker`, `pahole`, `readelf`, `objdump`, and `nm` as the available evidence set.

5. Compile at least one downstream consumer against the new headers. When binary compatibility is promised, also run an old binary against the new library.
6. Record the result in the gate report: supported contract, tools used, exact artifacts compared, incompatible changes, intentional breaks, and migration notes.

Do not claim ABI compatibility from unit tests alone. Unit tests can pass while exported names, layout, exception behavior, or allocator boundaries are broken.

## C++ ABI Rules

- Prefer stable C ABI boundaries for plugins, SDKs, and cross-compiler consumers.
- Do not expose STL containers, exceptions, RTTI-dependent classes, allocator ownership, or inline implementation details across a long-lived binary boundary unless the project already commits to that ABI.
- For C++ library internals, use PIMPL, hidden visibility, version scripts, or explicit export maps when the project supports them.
- Changing class layout, virtual functions, enum size, inline function behavior, template instantiations, exception specifications, or public data members can be ABI-visible.

## Header Hygiene

- Minimize transitive includes in public headers.
- Use forward declarations where correct.
- Keep macros scoped and prefixed.
- Avoid leaking platform headers through public API.
- Ensure C headers are C++ compatible when intended:

```c
#ifdef __cplusplus
extern "C" {
#endif

/* declarations */

#ifdef __cplusplus
}
#endif
```

## Cross Compilation

For cross builds, record:

- Toolchain file or cross file.
- Sysroot.
- Target triple.
- Emulator or hardware test path.
- Which tests were compiled only versus executed.
