#!/bin/sh
# scripts/make-app-icon.sh — rebuild resources/ChadVis.icns from the SVG.
#
# The checked-in .icns is the artifact a release build copies into the macOS
# bundle (T0088). This script is the one fact about how it was made: run it
# when the SVG changes, then commit the new .icns. Requires rsvg-convert
# (MacPorts: `sudo port install librsvg-devel`) and macOS iconutil/sips.
#
# Nothing in the build calls this: a CI Linux/Windows box has no iconutil, and
# an app icon is a checked-in asset, not a build product. That keeps the
# release build free of a rasteriser dependency.

set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SVG="$ROOT/resources/icons/chadvis-projectm-qt.svg"
OUT="$ROOT/resources/ChadVis.icns"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/chadvis-icon.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

command -v rsvg-convert >/dev/null || { echo "rsvg-convert required" >&2; exit 1; }
command -v iconutil >/dev/null || { echo "iconutil required (macOS only)" >&2; exit 1; }

ICONSET="$WORK/ChadVis.iconset"
mkdir -p "$ICONSET"
# Standard macOS iconset: each size plus its @2x retina twin. Apple's maximum
# is 512x512@2x (1024px); larger entries are silently dropped by iconutil.
for sz in 16 32 128 256 512; do
    rsvg-convert -w "$sz" -h "$sz" "$SVG" -o "$ICONSET/icon_${sz}x${sz}.png"
    double=$((sz * 2))
    rsvg-convert -w "$double" -h "$double" "$SVG" \
        -o "$ICONSET/icon_${sz}x${sz}@2x.png"
done

iconutil -c icns "$ICONSET" -o "$OUT"
file "$OUT"
