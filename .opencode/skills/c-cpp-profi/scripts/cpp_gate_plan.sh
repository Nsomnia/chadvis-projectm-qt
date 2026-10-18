#!/usr/bin/env bash
set -u

root="${1:-.}"

if [ ! -d "$root" ]; then
  printf 'error: not a directory: %s\n' "$root" >&2
  exit 2
fi

printf 'C/C++ gate plan for %s\n' "$root"
printf '\n'

if [ -f "$root/CMakePresets.json" ]; then
  cat <<'PLAN'
Build system: CMake with presets

Inspect:
  cmake --list-presets
  cmake --list-presets=build
  cmake --list-presets=test

Run:
  cmake --preset <configure-preset>
  cmake --build --preset <build-preset>
  ctest --preset <test-preset> --output-on-failure
PLAN
elif [ -f "$root/CMakeLists.txt" ]; then
  cat <<'PLAN'
Build system: CMake without detected presets

Run:
  cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  cmake --build build/debug
  ctest --test-dir build/debug --output-on-failure
PLAN
elif [ -f "$root/meson.build" ]; then
  cat <<'PLAN'
Build system: Meson

Run:
  meson setup build/debug --buildtype=debug
  meson compile -C build/debug
  meson test -C build/debug --print-errorlogs
PLAN
elif [ -f "$root/Makefile" ]; then
  cat <<'PLAN'
Build system: Makefile

Inspect first:
  make help

Likely gates:
  make
  make test
PLAN
else
  cat <<'PLAN'
Build system: not detected

Next steps:
  read README/build docs
  inspect CI config
  avoid inventing a build system unless requested
PLAN
fi

cat <<'PLAN'

Additional gates to consider:
  clang-tidy -p <build-dir> <changed-files>
  scan-build <build-command>
  ASan+UBSan build and test
  TSan build and test for concurrency changes
  Helgrind/DRD or rr/stress replay for deadlocks and flaky concurrency
  lock-order graph and atomic memory-order proof for threaded changes
  fuzz harness or corpus replay for input parsers
  benchmark/profile for performance claims
  refactor isomorphism card, callsite census, ABI/API check, and golden replay for simplification claims
PLAN
