#!/usr/bin/env bash
# Build a single Qt prefix out of the formulae Homebrew installed separately.
#
# WHY THIS EXISTS
#
# `brew upgrade` left `qt` (the umbrella) pinned at 6.11.1 while qtbase,
# qtdeclarative, qtmultimedia and qttools all moved to 6.12.0. The umbrella's
# cmake files are 6.11.1 and its bin/ is missing binaries they reference, so a
# find_package(Qt6) against it fails with an imported target that "references a
# file which does not exist" -- which reads like a broken project and is a
# half-finished package manager state.
#
# The reverse is also true: Qt6Config.cmake only looks for its components
# BESIDE ITSELF, so pointing CMAKE_PREFIX_PATH at all three prefixes does not
# help either -- Qt6Multimedia is simply not next to qtbase's Qt6Config.cmake.
#
# So: symlink the union of the 6.12.0 prefixes into one tree. Nothing is copied
# and nothing in Homebrew is touched. Delete .qt-stitch/ to undo it entirely.
#
# When `brew upgrade qt` finally completes, this directory is obsolete and the
# plain `--qt /usr/local/opt/qt` works again.
#
# Usage:  scripts/qt-stitch.sh && ./build.sh --fast --tests --qt "$PWD/.qt-stitch"

set -euo pipefail

PREFIXES=(
    /usr/local/opt/qtbase
    /usr/local/opt/qtdeclarative
    /usr/local/opt/qtmultimedia
    /usr/local/opt/qttools
)

STITCH="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.qt-stitch"

# merge_dir <relative dir> -- one real directory, every child from every prefix
# symlinked in. Real dir, because two prefixes contributing to the same
# subdirectory (share/qt/libexec is in both qtbase and qttools) cannot both own
# a symlink of the same name.
# merge_dir <relative dir> [child-to-skip] -- one real directory, every child
# from every prefix symlinked in. Real dir, because two prefixes contributing to
# the same subdirectory (share/qt/libexec is in both qtbase and qttools) cannot
# both own a symlink of the same name.
#
# ORDERING IS LOAD-BEARING. Deepest path first. Merging share/qt before
# share/qt/libexec symlinks `libexec` at one prefix's copy, and the next call
# then writes THROUGH it into Homebrew's tree -- which is how a draft of this
# script left seven *.d directories inside qtbase. The skip list is what keeps
# the parent from stealing a child that has already been materialised.
merge_dir() {
    local rel="$1" skip="${2:-}" dest="$STITCH/$1"
    mkdir -p "$dest"
    for p in "${PREFIXES[@]}"; do
        local src="$p/$rel"
        [ -d "$src" ] || continue
        for child in "$src"/*; do
            [ -e "$child" ] || continue
            [ "$(basename "$child")" = "$skip" ] && continue
            ln -sfnh "$child" "$dest/$(basename "$child")" 2>/dev/null || true
        done
    done
}

# skip_any <name> <skip-list> -- membership test without an associative array,
# which bash 3.2 (still the system bash on this machine) does not have.
skip_any() {
    local n="$1" list="$2" item
    for item in $list; do
        [ "$n" = "$item" ] && return 0
    done
    return 1
}

# merge_cmake -- merge lib/cmake with an explicit owner per component name.
#
# Written as its own function because plain merge_dir cannot express this: two
# prefixes ship a directory of the same name and only one of them owns it.
# qtmultimedia has lib/cmake/Qt6Qml containing nothing but its QmlPlugins;
# qtdeclarative has the real one with Qt6QmlConfig.cmake. `ln` is last-wins, so
# whichever prefix is listed last takes the name -- which is how the stitch ended
# up with a Qt6Qml that had no config in it and find_package failing for no
# visible reason.
#
# The rule is simply: a prefix may claim a component name only if that name has
# a Qt6<name>Config.cmake in it. Everything else is a plugin directory.
merge_cmake() {
    local dest="$STITCH/lib/cmake"
    mkdir -p "$dest"
    for p in "${PREFIXES[@]}"; do
        [ -d "$p/lib/cmake" ] || continue
        for child in "$p/lib/cmake"/*; do
            [ -e "$child" ] || continue
            local name; name="$(basename "$child")"
            skip_any "$name" "Qt6 plugins libexec qml metatypes mkspecs" && continue
            if [ -d "$child" ] && [ ! -f "$child/${name}Config.cmake" ]; then
                continue   # a plugins-only directory: not a component
            fi
            ln -sfnh "$child" "$dest/$name" 2>/dev/null || true
        done
    done
}

echo "stitching into $STITCH"

# Inner directory FIRST, then the outer one skipping it. Reversed, merging
# lib/cmake symlinks a `Qt6` pointing at qtbase's, and the next merge_dir writes
# THROUGH it into Homebrew's own tree. The first version of this script did
# exactly that.
# Innermost first. See the ordering note on merge_dir -- reversing these two
# blocks writes into Homebrew's own directories rather than into the stitch.
merge_dir "lib/cmake/Qt6"      # what Qt6Config.cmake looks beside itself for
merge_cmake                    # Qt6Core, Qt6Qml, Qt6Multimedia, ... side by side
merge_dir "lib"                # frameworks and dylibs
merge_dir "share/qt/libexec"   # moc and syncqt: qtbase and qttools, one dir
merge_dir "share/qt/plugins"   # platform plugins: qtbase and qtmultimedia
merge_dir "share/qt/qml/Qt/labs/platform"
merge_dir "share/qt/qml/Qt/labs"
merge_dir "share/qt/qml/Qt"
merge_dir "share/qt/qml"
merge_dir "share/qt/metatypes"  # qt6core_metatypes.json, linked by target_link_libraries
merge_dir "share/qt/mkspecs"    # qglobal.h, in Qt6Core's INTERFACE_INCLUDE_DIRECTORIES
merge_dir "share/qt"
merge_dir "bin"

mkdir -p "$STITCH/include"
for p in "${PREFIXES[@]}"; do
    [ -d "$p/include" ] || continue
    for child in "$p/include"/*; do
        [ -e "$child" ] || continue
        ln -sfn "$child" "$STITCH/include/$(basename "$child")" 2>/dev/null || true
    done
done

# Report rather than assume. A stitch that silently misses one component gives
# the same confusing find_package failure it was built to fix.
missing=0
for comp in Core Gui Network Sql DBus Qml Quick QuickControls2 Multimedia; do
    if [ ! -e "$STITCH/lib/cmake/Qt6$comp/Qt6${comp}Config.cmake" ]; then
        echo "MISSING Qt6$comp"
        missing=1
    fi
done
[ "$missing" -eq 0 ] && echo "all 8 components present"