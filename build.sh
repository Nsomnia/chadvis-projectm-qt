#!/usr/bin/env bash
# ChadVis portable build wrapper (macOS / Linux).
#
# Usage: ./build.sh [--debug|--release] [--clean]
#
# Locates Qt6 via $CHADVIS_QT_PATH or common Homebrew/system prefixes,
# configures a Ninja build into ./build, then builds.
# The legacy Arch tooling in scripts/build.zsh remains untouched.

set -euo pipefail

BUILD_TYPE="Release"
CLEAN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug)   BUILD_TYPE="Debug"; shift ;;
    --release) BUILD_TYPE="Release"; shift ;;
    --clean)   CLEAN=1; shift ;;
    -h|--help)
      echo "Usage: ./build.sh [--debug|--release] [--clean]"
      exit 0 ;;
    *)
      echo "build.sh: unknown option '$1' (see --help)" >&2
      exit 1 ;;
  esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT}/build"

# Locate Qt6: env override first, then common Homebrew prefixes (Apple Silicon
# and Intel) and the typical Linux layout.
QT_PATH="${CHADVIS_QT_PATH:-}"
if [[ -z "$QT_PATH" ]]; then
  for candidate in \
      /opt/homebrew/opt/qt \
      /usr/local/opt/qt \
      /usr/lib/qt6; do
    if [[ -d "$candidate" ]]; then
      QT_PATH="$candidate"
      break
    fi
  done
fi

JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

CMAKE_ARGS=(-G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE")
if [[ -n "$QT_PATH" ]]; then
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$QT_PATH")
fi

# Clean = archive the old build tree (housekeeping rule: never delete outright).
if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
  GRAVEYARD="${ROOT}/.backup_graveyard"
  mkdir -p "$GRAVEYARD"
  STAMP="$(date +%Y%m%d-%H%M%S)"
  echo "Archiving ${BUILD_DIR} -> ${GRAVEYARD}/build-${STAMP}"
  mv "$BUILD_DIR" "${GRAVEYARD}/build-${STAMP}"
fi

echo "== Configure (${BUILD_TYPE}, Qt: ${QT_PATH:-autodetect}) =="
cmake -B "$BUILD_DIR" "${CMAKE_ARGS[@]}" "$ROOT"

echo "== Build (${JOBS} jobs) =="
cmake --build "$BUILD_DIR" -j "$JOBS"

echo "== Done: ${BUILD_DIR}/chadvis-projectm-qt =="
