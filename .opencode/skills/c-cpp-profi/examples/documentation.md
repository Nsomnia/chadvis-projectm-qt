# Example: Documentation As A Test Gate (libgeohash)

Use when a request writes or revises a README, an architecture note, header API
contracts, or a changelog for a C/C++ library. Author from inventory facts, then
run each surface through `cpp_docs_check.py` for its kind. See
[DOCUMENTATION.md](../references/DOCUMENTATION.md) for the full authoring order
and completion standard. This whole file passes `--kind readme`, `--kind api`,
and `--kind changelog`, and is slop-free.

Subject: `libgeohash`, a small C library that encodes/decodes geohashes,
shipped as `libgeohash.so` with a public C ABI consumed by three callers.

## README excerpt

`libgeohash` encodes a latitude/longitude pair into a base-32 geohash string and
decodes it back, for application authors embedding a spatial index.

## Build from source

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build            # 96 tests, GCC 14 / Clang 18, C23
```

Build dependencies: CMake >= 3.20, a C23 compiler. No runtime dependencies.

## Install

```sh
cmake --install build --prefix /usr/local
# installs include/geohash.h, libgeohash.so (SONAME libgeohash.so.2), and the
# pkg-config file geohash.pc
```

## Usage

The smallest program that links the library and prints visible output. Compile
with `cc quick.c $(pkg-config --cflags --libs geohash) -o quick`.

```c
#include <geohash.h>
#include <stdio.h>

int main(void) {
    char out[12];
    if (geohash_encode(57.64911, 10.40744, 9, out, sizeof out) != 0)
        return 1;
    printf("%s\n", out);   /* prints: u4pruydqq */
    return 0;
}
```

## License

SPDX-License-Identifier: MIT.

## Architecture note

- Module map: `encode.c` and `decode.c` export the public surface; `bits.c` is
  internal (`-fvisibility=hidden`). No symbol leaks beyond the version script.
- Ownership and lifetime: `geohash_encode` writes into a caller-owned buffer and
  retains nothing; the library allocates no heap memory on the encode path.
- Threading: every function is safe to call concurrently because it touches no
  shared state and keeps no global parser singleton.
- Error model: return-code only; no exceptions cross the `extern "C"` boundary.
- ABI/versioning: SemVer; an ABI break is a MAJOR bump plus a `SONAME` bump.

## API contract block

The header documents the full contract for each public symbol.

```c
/**
 * @fn geohash_encode
 * @brief Encode a coordinate into a base-32 geohash string.
 *
 * Ownership: writes into caller-owned @p out; the callee retains no pointer and
 *   allocates nothing. The caller owns and frees @p out.
 * Lifetime: @p out must stay valid for the duration of the call only.
 * Thread-safety: safe to call concurrently; touches no shared state.
 * Error/returns: 0 on success; -1 with errno==EINVAL on bad input, errno==
 *   ERANGE when @p cap is too small for @p precision + 1 bytes.
 * Preconditions: out != NULL; 1 <= precision <= 12; lat in [-90,90].
 */
int geohash_encode(double lat, double lon, int precision, char *out, size_t cap);
```

## Changelog excerpt

```markdown
## [2.0.0] - 2026-05-29
### Changed
- BREAKING ABI: geohash_handle gained an allocator field; SONAME 1 -> 2.
  Recompile required. abidiff report attached to the release.
### Added
- geohash_encode() bounds-checked buffer length via the new cap parameter.
### Fixed
- Off-by-one in decode precision that truncated 12-char hashes (regression test).
```

## Evidence Packet

```text
# C/C++ Gate Report

## Change Scope
- Issue/task: document libgeohash (README + architecture + header API + changelog)
- Touched files: README.md, docs/architecture.md, include/geohash.h, CHANGELOG.md
- Public API/ABI touched: yes (documented SONAME 1 -> 2 break)
- Performance claim: no

## Commands
| Gate | Status | Command | Evidence |
|---|---|---|---|
| docs | passed | python3 .opencode/skills/c-cpp-profi/scripts/cpp_docs_check.py examples/documentation.md --kind readme | cpp_docs_check.py: PASS (readme); sections build/usage/license present; usage snippet compiled with cc + pkg-config, printed u4pruydqq |
| docs-as-tests | passed | python3 .opencode/skills/c-cpp-profi/scripts/cpp_docs_check.py examples/documentation.md --kind api && ... --kind changelog | cpp_docs_check.py: PASS (api: geohash_encode ownership/thread-safety/error complete); cpp_docs_check.py: PASS (changelog: 2.0.0 ABI-break MAJOR + SONAME bump recorded); slop pass clean |
```

## Refusal Conditions

- A usage snippet that does not compile, or a build section naming a build
  system the repo does not use.
- A public symbol missing its ownership, thread-safety, or error/returns
  contract field.
- A changelog with no versioned section, or an ABI break that did not bump the
  MAJOR version and the `SONAME`.
- Any banned marketing token surviving the slop pass.
