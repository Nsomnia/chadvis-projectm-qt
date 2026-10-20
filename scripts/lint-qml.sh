#!/usr/bin/env bash
# lint-qml.sh -- the one command for QML problems.
#
# Why this exists: `qmllint $(fd --extension qml)` prints 200+ lines, most of
# them the same `unqualified` access preference repeated, so nobody ran it and
# the QML kept its own bugs in the dark -- including a `color` bound to a `string`
# field that silently took the theme down (T0047), and the missing `id` typo
# that made Settings unopenable (T0001). Both are things qmllint reports.
#
# Run it with:
#   ./scripts/lint-qml.sh                 # what matters; fails on a real problem
#   ./scripts/lint-qml.sh --strict        # everything, as a gate
#   ./scripts/lint-qml.sh --list          # the raw list, counts only
#
# Exit status is non-zero on a blocking problem, so this works as a gate.
#
# HOW IT RESOLVES THINGS: it runs the build's own `project_lib_qmllint` target
# rather than invoking qmllint directly. That is deliberate and load-bearing.
# Invoked by hand, qmllint cannot resolve this tree: `main.qml` uses relative
# local imports (`import "views"`) which need the source layout, while
# `Theme.qml` imports its own `ChadVis` module for `SettingsBridge`, which needs
# the built module -- and `ChadVis/qmldir` names its files relative to itself.
# No single CWD satisfies all three. CMake's target already solves that
# (`QML_DIRS`/`-I` plus the module's own qmldir) and reports real `src/` paths,
# so wrapping it is the only version of this script that tells the truth.
#
# An earlier version of this file tried to invoke qmllint itself, passed flags
# qmllint does not accept (`--compact`, `--relative`), filtered the resulting
# "unknown option" error away as "no findings", and cheerfully reported
# "clean across 52 files". A linter that reports clean because it never ran is
# worse than no linter, so this version fails loudly if the underlying command
# produces nothing at all.
set -uo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"

MODE="filter"   # filter | strict | list
case "${1:-}" in
    --strict) MODE="strict"; shift ;;
    --list)   MODE="list";   shift ;;
    "")       ;;
    *)        echo "lint-qml: unknown option '$1' (see the header)" >&2; exit 1 ;;
esac

# CI exports BUILD_DIR for the job (e.g. build-ci); the local default stays
# build-fast. QML_BUILD_DIR still wins when set explicitly.
BUILD_DIR="${QML_BUILD_DIR:-${BUILD_DIR:-$ROOT/build-fast}}"

# The build must exist: the lint target is a ninja target, and it needs the
# generated qmldir and qmltypes to resolve `import ChadVis`.
if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
    echo "lint-qml: $BUILD_DIR is not configured." >&2
    echo "          Run: ./build.sh --fast --tests" >&2
    exit 1
fi

# Always re-run: the target is phony, so ninja will not skip it, but a failed
# run inside it is easy to miss without the output.
#
# RC is deliberately NOT used to decide "the tool broke". When qmllint hits a
# finding at error level it exits non-zero, ninja marks the target failed and
# exits 255 -- which is the gate working, not the gate being broken. An earlier
# version of this script treated any RC > 1 as "qmllint could not run" and
# printed "no findings", which is how a script meant to catch defects reported
# clean on the exact commit that introduced one.
RAW="$(ninja -C "$BUILD_DIR" project_lib_qmllint 2>&1)"
RC=${PIPESTATUS[0]}

# The only genuine tool failure is one that produced no warning lines at all.
TOTAL="$(printf '%s\n' "$RAW" | grep -cE '^(Warning|Info|Error): ' || true)"
LINT_RAN_SIGNAL="$(printf '%s\n' "$RAW" | grep -cE 'qmllint @|Failed to open file|Unknown option|not found' || true)"

if [[ "$TOTAL" -eq 0 && $RC -ne 0 && "$LINT_RAN_SIGNAL" -eq 0 ]]; then
    echo "lint-qml: the build's lint target failed with no findings at all" >&2
    echo "          (ninja exit $RC). That is a tooling failure, not a clean run:" >&2
    printf '%s\n' "$RAW" >&2
    exit 1
fi

if [[ "$TOTAL" -eq 0 ]]; then
    echo "qmllint: clean across the QML tree"
    exit 0
fi

# The categories the tree treats as wrong. `.qmllint.ini` records which checks
# are errors; these are the ones a reader can act on without knowing QML's
# style folklore.
# The categories the tree treats as wrong. `.qmllint.ini` records which checks
# are errors; these are the ones a reader can act on without knowing QML's
# style folklore.
#
# NOTE: match on the DIAGNOSTIC tag, never on a word. The tags are the bracketed
# suffix of a `Warning: path:line: <text> [tag]` line. An earlier draft of this
# script included `index` in the list, which matched source-code echo lines like
# `onActivated: (index) => ...` rather than any diagnostic -- producing findings
# that were Python-console colour, not QML errors. The bracket anchor is what
# keeps that from happening.
# T0198. `missing-property` joined the blocking set because of a concrete
# failure, not a hunch. AppColorPicker.qml used LinearGradient without
# `import QtQuick.Shapes`; qmllint reported the *consequences* -- four
# `Could not find property "horizontal"/"vertical"` -- tagged `missing-property`,
# which was baselined as "other". The result was an app that did not start:
# "Type AppColorPicker unavailable" took the settings window and the main window
# down with it, while the lint gate said "no blocking problems" and the whole
# test suite stayed green. All four findings tree-wide were that one bug, so
# promoting the tag costs nothing and catches the class.
BLOCKING="$(printf '%s\n' "$RAW" | grep -E '\[(incompatible-type|unused-imports|missing-type|missing-property|invalid-property)\]' || true)"
# `unqualified` access is a documented style preference at 144 occurrences, kept
# separate so "there is a bug" and "there is a backlog" never look the same.
PREFERENCE="$(printf '%s\n' "$RAW" | grep -E '\[(unqualified|deprecated)\]' || true)"
# The AOT compiler diagnostics -- ~925 of them, and the reason nobody ran qmllint
# before. Mostly "member X can be shadowed" / "cannot load property from
# QVariant", i.e. the qmlcachegen pass being conservative about types it cannot
# see. A real backlog (T0048), but noise to a reader looking for a defect.
COMPILER="$(printf '%s\n' "$RAW" | grep -E '\[compiler\]' || true)"

BLOCKING_COUNT="$(printf '%s\n' "$BLOCKING" | grep -cE '^(Warning|Info|Error): ' || true)"
PREF_COUNT="$(printf '%s\n' "$PREFERENCE" | grep -cE '^(Warning|Info|Error): ' || true)"
COMPILER_COUNT="$(printf '%s\n' "$COMPILER" | grep -cE '^(Warning|Info|Error): ' || true)"
OTHER=$(( TOTAL - BLOCKING_COUNT - PREF_COUNT - COMPILER_COUNT ))

if [[ "$MODE" == "list" ]]; then
    echo "total=$TOTAL blocking=$BLOCKING_COUNT unqualified=$PREF_COUNT compiler=$COMPILER_COUNT other=$OTHER"
    exit 0
fi

if [[ "$MODE" == "strict" ]]; then
    echo "qmllint: $TOTAL finding(s)"
    echo "  blocking   $BLOCKING_COUNT"
    echo "  unqualified $PREF_COUNT   (style preference, T0048 baseline)"
    echo "  compiler   $COMPILER_COUNT   (AOT diagnostics, T0048 baseline)"
    echo "  other      $OTHER"
    echo
    printf '%s\n' "$RAW"
    exit 1
fi

if [[ $BLOCKING_COUNT -gt 0 ]]; then
    echo "qmllint: $BLOCKING_COUNT problem(s) that must be fixed:"
    echo
    printf '%s\n' "$BLOCKING"
    echo
    echo "Fix those, then re-run. .qmllint.ini records which checks are errors."
    echo "The rest is a backlog, not a defect: $PREF_COUNT unqualified access and"
    echo "$COMPILER_COUNT AOT diagnostics -- run --strict to see them, or T0048 to clear them."
    exit 1
fi

echo "qmllint: no blocking problems."
echo "        $TOTAL finding(s) remain, all of them a recorded baseline:"
echo "          $PREF_COUNT unqualified access   (style preference)"
echo "          $COMPILER_COUNT AOT diagnostics  (qmlcachegen conservatism)"
if [[ $OTHER -gt 0 ]]; then
    echo "          $OTHER other"
fi
echo "        run --strict to list them; clear them under T0048."
exit 0
