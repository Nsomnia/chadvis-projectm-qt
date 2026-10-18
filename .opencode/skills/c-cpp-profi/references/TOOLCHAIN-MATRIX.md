# C/C++ Toolchain Matrix

## Purpose

This matrix defines the tool families that `c-cpp-profi` must consider before an agent claims C/C++ work is done. It is intentionally enforcing: missing tools are not silent skips. They become explicit evidence gaps with installation notes or follow-up beads.

The project contract still wins. Do not migrate a project to a new tool just because it appears here.

## Source Anchors Checked

- Clang sanitizers and sanitizer coverage: <https://clang.llvm.org/docs/>
- libFuzzer: <https://llvm.org/docs/LibFuzzer.html>
- clang-tidy: <https://clang.llvm.org/extra/clang-tidy/>
- CMake, CTest, CPack, presets: <https://cmake.org/cmake/help/latest/>
- Meson tests and introspection: <https://mesonbuild.com/>
- Valgrind/Memcheck: <https://valgrind.org/docs/manual/>
- AFL++: <https://aflplus.plus/docs/>
- CodeQL C/C++: <https://codeql.github.com/docs/codeql-language-guides/codeql-for-cpp/>
- libabigail: <https://sourceware.org/libabigail/manual/>
- include-what-you-use: <https://include-what-you-use.org/>
- Linux perf: <https://perfwiki.github.io/main/> and `man perf`
- Linux man-pages: <https://man7.org/linux/man-pages/>
- SEI CERT C and C++: <https://cmu-sei.github.io/secure-coding-standards/>
- C++ Core Guidelines: <https://github.com/isocpp/CppCoreGuidelines>

## Required Tool Families

| Family | Primary tools | Evidence produced | Required when | Common gap |
|---|---|---|---|---|
| Compiler front ends | GCC, Clang, MSVC, AppleClang, Intel oneAPI `icx`/`icpx` | compile diagnostics, standard support, warnings, target triples, object/binary output | every edit | one compiler passing does not prove portability |
| Build orchestrators | CMake, Meson, Make, Ninja, Autotools, Bazel, build2, xmake | configure/build/test logs, compile database, presets/options, build graph | every non-trivial edit | build succeeds but tests are undiscovered |
| Package/dependency managers | vcpkg manifest mode, Conan 2, Spack, pkg-config, CMake FetchContent/CPM, distro packages | resolved versions, lock/baseline, profiles/triplets, link flags, transitive deps | external deps, packaging | hidden vendored code or ABI mismatch |
| Format/style | clang-format, project scripts, cmake-format, gersemi, uncrustify, astyle, `git diff --check` | deterministic formatting diff | style-only or touched style-sensitive code | imposing new style over project style |
| Static analyzers | clang-tidy, Clang Static Analyzer, cppcheck, CodeQL, MSVC `/analyze`, Infer, Semgrep, PVS-Studio, Coverity, CodeSonar, ECLAIR | findings with file/line and CWE/rule class, SARIF/XML/HTML reports | memory, input, concurrency, security, public API | trusting exit code without reading output |
| Include hygiene | include-what-you-use, clangd include-cleaner, header self-compile tests | redundant/missing includes, self-contained header proof | public headers, large C++ builds | removing includes without header self-test |
| Sanitizers | ASan, UBSan, LSan, MSan, TSan, HWASan, CFI, SafeStack, RealtimeSanitizer where supported | runtime failures with stack traces | memory, UB, concurrency, dispatch integrity | combining incompatible sanitizers |
| Dynamic memory tools | Valgrind Memcheck, massif, callgrind, heaptrack, Dr. Memory, Application Verifier | leaks, invalid reads/writes, uninitialized values, heap profiles | high memory risk, allocator, C APIs | Valgrind slow path skipped after sanitizers pass |
| Fuzzers | libFuzzer, AFL++, honggfuzz, FuzzTest, libprotobuf-mutator, OSS-Fuzz/ClusterFuzzLite | crashes, minimized reproducers, corpus coverage | parsers, decoders, protocols, file formats, untrusted bytes | no regression test from crash |
| Test frameworks | CTest, Meson test, GoogleTest, Catch2, doctest, Criterion, Unity, CMocka, project runners | pass/fail logs, JUnit/XML, fixtures, regression proof | every behavior change | no focused test for fixed defect |
| Coverage | llvm-cov, gcov/gcovr, lcov/genhtml, sancov | line/branch/function coverage, HTML/XML summaries | tests/fuzz claims | coverage used as quality proof instead of gap map |
| Debuggers | gdb, lldb, rr, WinDbg, coredumpctl | stack traces, watchpoints, record/replay, core notes, register/local state | crash/hang/root cause | fixing without minimized reproducer |
| Profilers | perf, Google Benchmark, VTune, Instruments, Windows Performance Analyzer, Tracy, callgrind, cachegrind, massif, heaptrack, bpftrace, FlameGraph/Hotspot | hotspots, counters, cache misses, syscalls, heap churn, benchmark JSON | performance claims | microbenchmark unrelated to production path |
| ABI/API tools | libabigail `abidiff`, abi-dumper, abi-compliance-checker, `nm`, `readelf`, `objdump`, `c++filt`, `dumpbin`, `pahole` | symbol/layout/vtable/visibility deltas | shared libs, plugins, SDK, FFI | `nm` is only a smoke test, not ABI proof |
| Link/load tools | ldd, otool, dumpbin, patchelf, chrpath, ld.so diagnostics, `LD_DEBUG`, `DYLD_PRINT_LIBRARIES` | dependency graph and runtime resolution | packaging, plugins, cross-platform | build links but runtime loader fails |
| Security/hardening | CodeQL, CERT/MISRA checkers, checksec, hardening-check, compiler/linker flags | CWE/rule findings, RELRO/PIE/stack protector status | security-sensitive binaries | hardening flags added without runtime test |
| Concurrency tools | TSan, helgrind/drd, rr, stress runners, lock-order audit, atomics review | races/deadlocks/repro traces | threads, atomics, callbacks, async bridges | no concrete interleaving proof |
| Documentation | Doxygen, Sphinx+Breathe/Exhale, clang-doc, scdoc/ronn, MSVC `/doc`, mdBook, man pages, CMake package docs | generated API docs and examples, broken refs, doc warnings | public API/SDK | docs compile but examples do not |
| SBOM/SCA/secrets | Syft, Grype, Trivy, OSV-Scanner, cve-bin-tool, Gitleaks | SBOM, CVE list, package coordinates, secret findings | releases, vendored deps, supply-chain work | C/C++ vendored deps are hard to identify |
| Safety/formal/embedded | Frama-C, CBMC, TrustInSoft, Astrée, PC-lint, MISRA/CERT checkers, sparse, smatch, Coccinelle, KASAN/KCSAN/UBSAN kernel, QEMU/HIL | proof conditions, rule violations, kernel/static findings, target execution logs | safety-critical, embedded, kernel/driver code | licensed tools or target hardware unavailable |
| GPU/HPC | NVIDIA Compute Sanitizer, ROCm tools, MPI profilers/sanitizers, OpenMP tooling | kernel memory/race findings, GPU traces, distributed timing | CUDA/HIP/MPI/OpenMP code | host-only tools miss device/distributed failures |
| Native UI/artifacts | screenshot tools, perceptual diff, terminal snapshots, image/video diff, `cpp_pixel_diff.py` | golden artifacts and thresholded diffs | rendering/UI/font/image/video/CAD | manual "looks right" claim |

## Local Environment Snapshot

Observed on 2026-05-24 and refreshed on 2026-05-25:

| Tool | Status |
|---|---|
| `meson` | available, version 1.7.0 |
| `clang-tidy` | available, LLVM 20.1.8 |
| `cppcheck` | available, 2.17.1 |
| `valgrind` | available, 3.25.1 |
| `shellcheck` | available, 0.10.0 |
| `ctest` | `/home/durakovic/.local/bin/ctest` is broken; `/usr/bin/ctest` works |
| `ffmpeg`, `ffprobe` | available |
| Python Pillow | available |
| `magick`, `compare`, `perceptualdiff` | unavailable in this environment |
| `abi-dumper`, `abi-compliance-checker`, `pahole` | available after tool install; forward-tested on zlib and INIReader |
| `abidiff` | available at `/usr/bin/abidiff`, version 2.8.0 from installed `abigail-tools`; also forward-tested earlier from locally extracted Ubuntu `abigail-tools` + `libabigail7` packages |
| Universal Ctags | not on system `PATH`; forward-tested from locally extracted Ubuntu `universal-ctags` with a temporary `ctags` shim; installed `ctags` remains Exuberant Ctags |

## Canonical Command Shapes

### CMake

```bash
cmake --version
ctest --version
cmake --list-presets || true
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

### Meson

```bash
meson setup build/debug --buildtype=debug
meson compile -C build/debug
meson test -C build/debug --print-errorlogs
meson introspect build/debug --targets
meson setup build/asan-ubsan --buildtype=debug -Db_sanitize=address,undefined
meson compile -C build/asan-ubsan
meson test -C build/asan-ubsan --print-errorlogs
```

### Static analysis

```bash
clang-tidy -p build/debug <changed-file> --quiet
cppcheck --enable=warning,style,performance,portability --std=c++23 <changed-paths>
scan-build cmake --build build/debug
codeql database create codeql-db --language=cpp --command='<build-command>'
codeql database analyze codeql-db codeql/cpp-queries:codeql-suites/cpp-security-and-quality.qls --format=sarifv2.1.0 --output=codeql.sarif
run-clang-tidy.py -p build/debug <changed-paths>
```

### Dynamic analysis

```bash
CC=clang CXX=clang++ cmake -S . -B build/asan-ubsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/asan-ubsan
ctest --test-dir build/asan-ubsan --output-on-failure
valgrind --error-exitcode=99 --leak-check=full <representative-test-or-binary>
```

### Fuzzing

```bash
clang++ -g -O1 -fsanitize=fuzzer,address,undefined <harness>.cc <sources> -o fuzz_target
./fuzz_target corpus -max_total_time=300 -print_final_stats=1
./fuzz_target -merge=1 corpus new_corpus
afl-clang-fast++ -g -O1 -fsanitize=address,undefined <harness>.cc <sources> -o afl_target
afl-fuzz -i seeds -o findings -- ./afl_target @@
```

### ABI/API

```bash
bash .opencode/skills/c-cpp-profi/scripts/cpp_abi_snapshot.sh libafter.so
bash .opencode/skills/c-cpp-profi/scripts/cpp_abi_snapshot.sh libafter.so libbefore.so
nm -D --defined-only libbefore.so | c++filt > before.symbols
nm -D --defined-only libafter.so | c++filt > after.symbols
diff -u before.symbols after.symbols
abidiff libbefore.so libafter.so
abi-dumper libbefore.so -o before.abi -vnum before -all
abi-dumper libafter.so -o after.abi -vnum after -all
abi-compliance-checker -l <library-name> -old before.abi -new after.abi
printf '%s\n' include/public_api.h include/public_types.h > public-headers.txt
abi-dumper libbefore.so -o before-public.abi -vnum before -public-headers public-headers.txt
abi-dumper libafter.so -o after-public.abi -vnum after -public-headers public-headers.txt
abi-compliance-checker -l <library-name> -old before-public.abi -new after-public.abi
pahole -F dwarf -C <public_type> libbefore.so > before.layout
pahole -F dwarf -C <public_type> libafter.so > after.layout
diff -u before.layout after.layout
readelf -Ws libafter.so
objdump -T libafter.so
```

For `abi-dumper -public-headers`, pass a directory of public headers or a file containing one header path per line. Do not pass a single header path unless that file is intentionally a header-list file; otherwise `abi-dumper` may treat every line in the header as a filename and produce useless dumps.

If rich ABI tooling is unavailable, run the snapshot helper anyway and record the missing tool explicitly. Its symbol and ELF metadata evidence can catch removed exports, accidental visibility changes, SONAME/dependency drift, and obvious C linkage mistakes. It cannot prove C++ class layout, vtable compatibility, parameter type compatibility, inline/template API stability, exception ABI, allocator ownership, or semantic compatibility. If `abi-dumper -public-headers` is unavailable because Universal Ctags is missing, label the ABI Compliance Checker result as unfiltered and pair it with exported-symbol and `pahole` layout diffs.

### Native UI And Image Artifacts

```bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_pixel_diff.py baseline.png candidate.png --threshold 0
python3 .opencode/skills/c-cpp-profi/scripts/cpp_pixel_diff.py baseline.ppm candidate.ppm --threshold 0
ffmpeg -hide_banner -i baseline.png -i candidate.png -lavfi ssim=stats_file=ssim.log -f null -
ffmpeg -hide_banner -i baseline.png -i candidate.png -lavfi psnr=stats_file=psnr.log -f null -
ffmpeg -hide_banner -f x11grab -draw_mouse 0 -video_size 160x96 -i "$DISPLAY+0,0" -frames:v 1 capture.png
ffmpeg -i capture.mp4 -vf 'select=eq(n\,42)' -vframes 1 frame-042.png
```

Use exact threshold `0` for deterministic software-rendered artifacts. Use a narrow nonzero threshold only when the rendering backend, antialiasing, font rasterizer, color profile, or platform matrix justifies it. For GUI capture, prefer the project's real screenshot harness; otherwise record the display server, screen size/depth, window geometry, capture region, renderer backend, and cursor/compositor policy. If `magick`, `compare`, or `perceptualdiff` is unavailable, record the missing tool and use `cpp_pixel_diff.py` plus FFmpeg SSIM/PSNR as the available fallback.

### Dependency and supply-chain

```bash
conan graph info . --format=json
conan install . -pr:h <host-profile> -pr:b <build-profile> --build=missing --format=json
vcpkg install --x-manifest-root=. --triplet <triplet>
pkg-config --cflags --libs <package>
syft dir:. -o spdx-json=sbom.spdx.json
grype sbom:sbom.spdx.json
trivy fs --scanners vuln,secret,misconfig .
osv-scanner scan source -r .
cve-bin-tool <binary-or-source-tree>
```

### Include and dependency graph

```bash
include-what-you-use -p build/debug <file>
iwyu_tool.py -p build/debug <changed-files>
cmake --graphviz=deps.dot build/debug
ninja -C build/debug -t graph
ninja -C build/debug -t deps
clang-scan-deps -compilation-database build/debug/compile_commands.json
```

### Performance and debugging

```bash
perf stat -r 10 -- <command>
perf record -g -- <command>
perf report
valgrind --tool=callgrind <command>
valgrind --tool=massif <command>
heaptrack <command>
gdb --args <binary> <args>
rr record <command>
rr replay
coredumpctl gdb <binary>
```

## Internals and Manpage Reading Map

Agents working on native code must know when to consult these primary references:

| Area | Read |
|---|---|
| Process and loader | `man execve`, `man fork`, `man waitpid`, `man dlopen`, `man ld.so`, ELF psABI docs |
| Memory | `man malloc`, `man free`, `man mmap`, `man munmap`, `man mprotect`, `man brk`, allocator docs |
| Files and I/O | `man open`, `man read`, `man write`, `man pread`, `man poll`, `man epoll`, `man io_uring_setup` |
| Threads | `man pthreads`, `man pthread_create`, `man pthread_mutex_lock`, `man futex`, C/C++ atomics specs |
| Signals | `man signal`, `man sigaction`, `man signal-safety`, `man pthread_sigmask` |
| Time | `man clock_gettime`, `man timerfd_create`, platform monotonic clock docs |
| Networking | `man socket`, `man connect`, `man bind`, `man send`, `man recv`, `man getaddrinfo` |
| Linkage | `man ld`, `man ar`, `man nm`, `man objdump`, `man readelf`, MSVC `dumpbin` docs |
| Debugging | `man gdb`, `man perf`, `man valgrind`, `man core`, `man coredumpctl` |
| Standards | ISO C/C++ drafts as available, cppreference, SEI CERT C/C++, MISRA where licensed, C++ Core Guidelines |

## Optional Tool Policy

Commercial, formal, safety, kernel, GPU, and platform-specific tools are not required for every open-source project. They are required to be considered when the target domain needs them. If unavailable, record:

```text
not run: <tool/family> unavailable; relevant because <risk>; fallback used <fallback>; follow-up <bead/id>
```

Use strongest available fallbacks without pretending they prove the same thing. For example, `nm` and `readelf` are useful ABI smoke checks, but they do not replace `abidiff`; Valgrind is useful dynamic evidence, but it does not replace ASan+UBSan coverage of all tests.

## Enforcement Rules

1. Missing tool means "not run: missing `<tool>`", not omission.
2. A green build is not a green gate when tests are undiscovered.
3. Analyzer output must be read. Exit code alone is not enough.
4. Sanitizers must cover the touched path, not merely compile.
5. Every fuzz crash becomes a minimized regression test.
6. Every performance claim needs baseline, profile, one lever, and remeasure.
7. Every ABI-sensitive change needs `cpp_abi_snapshot.sh` or stronger symbol/layout evidence plus a recorded ABI gap for any unavailable rich comparison tool.
8. Every public header must compile standalone or have a documented exception.
9. Every security-sensitive change must map findings to CERT/Core Guideline/CWE-style categories.
10. Every handoff must list commands, outcomes, uncovered risk, and follow-up beads.
