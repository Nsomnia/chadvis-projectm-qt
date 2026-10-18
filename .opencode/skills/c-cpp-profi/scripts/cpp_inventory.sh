#!/usr/bin/env bash
set -u

root="${1:-.}"

if [ ! -d "$root" ]; then
  printf 'error=not_a_directory\n'
  printf 'path=%s\n' "$root"
  exit 2
fi

has_file() {
  [ -e "$root/$1" ] && printf 'yes' || printf 'no'
}

has_tool() {
  command -v "$1" >/dev/null 2>&1 && printf 'yes' || printf 'no'
}

tool_version_works() {
  command -v "$1" >/dev/null 2>&1 && "$1" --version >/dev/null 2>&1 && printf 'yes' || printf 'no'
}

count_files() {
  find "$root" -type f \( "$@" \) 2>/dev/null | wc -l | tr -d ' '
}

printf 'root=%s\n' "$root"
printf 'build.cmake=%s\n' "$(has_file CMakeLists.txt)"
printf 'build.cmake_presets=%s\n' "$(has_file CMakePresets.json)"
printf 'build.meson=%s\n' "$(has_file meson.build)"
printf 'build.make=%s\n' "$(has_file Makefile)"
printf 'build.bazel=%s\n' "$(has_file WORKSPACE)"
printf 'build.compile_commands=%s\n' "$(has_file compile_commands.json)"

printf 'count.c=%s\n' "$(count_files -name '*.c')"
printf 'count.cpp=%s\n' "$(count_files -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.C')"
printf 'count.headers=%s\n' "$(count_files -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx')"
printf 'count.cmake=%s\n' "$(count_files -name 'CMakeLists.txt' -o -name '*.cmake')"

for tool in cmake ctest ninja make meson clang clang++ gcc g++ cl clang-tidy scan-build cppcheck valgrind perf hyperfine heaptrack rg readelf objdump nm c++filt abidiff abi-dumper abi-compliance-checker pahole; do
  printf 'tool.%s=%s\n' "$tool" "$(has_tool "$tool")"
done

for tool in cmake ctest ninja meson clang clang++ gcc g++ clang-tidy cppcheck readelf objdump nm; do
  printf 'tool.%s_version_works=%s\n' "$tool" "$(tool_version_works "$tool")"
done
