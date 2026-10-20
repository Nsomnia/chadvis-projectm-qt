# Toolchain Templates

## Purpose

Use these templates when a target project wants a repeatable sanitizer or fuzzing entry point. Copy the smallest relevant asset and adapt it to the existing build system. Do not impose these files on a project that already has an equivalent workflow.

## Asset Index

| Asset | Use |
|---|---|
| `assets/cmake/CMakePresets.sanitizers.json` | Starting point for CMake configure/build/test presets |
| `assets/cmake/cmake/CppSystemsSanitizers.cmake` | Target-based CMake helper functions |
| `assets/cmake/fuzz/CMakeLists.libfuzzer.example.txt` | Minimal CMake libFuzzer target fragment |
| `assets/meson/native/asan-ubsan.ini` | Meson native file for ASan+UBSan |
| `assets/meson/native/tsan.ini` | Meson native file for ThreadSanitizer |
| `assets/meson/fuzz/meson.build.libfuzzer.example` | Minimal Meson libFuzzer target fragment |
| `assets/fuzz/libfuzzer_target.c` | Minimal C fuzz target |
| `assets/fuzz/libfuzzer_target.cc` | Minimal C++ fuzz target |

## CMake Use

If the project already has `CMakePresets.json`, merge only the relevant presets. Project-wide presets belong in `CMakePresets.json`; developer-local presets belong in `CMakeUserPresets.json`, which should not be committed.

For target-level wiring, prefer:

```cmake
include(cmake/CppSystemsSanitizers.cmake)
cpp_systems_enable_sanitizers(my_test_target SANITIZERS address undefined)
```

For fuzz targets:

```cmake
include(cmake/CppSystemsSanitizers.cmake)
add_executable(my_fuzzer EXCLUDE_FROM_ALL fuzz/my_fuzzer.cc)
target_link_libraries(my_fuzzer PRIVATE my_library)
cpp_systems_enable_libfuzzer(my_fuzzer SANITIZERS address undefined)
```

Run:

```bash
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan
ctest --preset asan-ubsan --output-on-failure
```

## Meson Use

Use native files when the project accepts Meson machine files:

```bash
meson setup build/asan-ubsan --native-file assets/meson/native/asan-ubsan.ini
meson compile -C build/asan-ubsan
meson test -C build/asan-ubsan --print-errorlogs
```

For fuzz targets, adapt `assets/meson/fuzz/meson.build.libfuzzer.example` into the target repo's `meson.build`.

## Fuzz Harness Rules

- Replace the placeholder call with the narrowest parser/API boundary.
- Keep the body deterministic and fast.
- Do not call `exit`, sleep, write logs, read network, or rely on wall-clock time.
- Create at least one seed input in a corpus directory.
- Convert every crash input into a regression test.

## Caveats

- ASan and TSan are separate campaigns.
- MSan requires instrumented dependencies; do not add a casual MSan preset unless the dependency story is known.
- `-fsanitize=fuzzer` is for Clang/libFuzzer builds. It links the fuzzing engine and should not be used on ordinary test binaries.
- Sanitizer runtimes are test/development tooling, not production hardening.
