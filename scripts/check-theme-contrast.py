#!/usr/bin/env python3
"""Measure the built-in theme palettes against WCAG contrast, without building.

T0191: the four palettes in src/qml/styles/ThemeDefinitions.qml were written from
the plan's §5 proposals and never looked at. This is the measurement that was
missing, and it needs neither Qt nor a running window -- Theme.qml's derivation
is pure arithmetic, so it is reimplemented here and the token set is parsed
straight out of the QML file. If the two ever disagree, the file's numbers win
and this script is the bug.

Run:    python3 scripts/check-theme-contrast.py
Fix:    python3 scripts/check-theme-contrast.py --suggest

Exit 0 when every gated pair clears. Exits 1 otherwise, so it can gate a build.

WHAT IT DOES NOT COVER: whether a theme looks good. It checks that text is
legible and that interactive colour is distinguishable -- not whether gruvbox
looks like gruvbox, and not whether two adjacent surfaces are far enough apart
to read as separate planes, which is a judgement no ratio answers. The
screenshot pass in docs/dev/manual-qa.md (MT-001-G) is still required.

Three bugs this script had before it was right, recorded because each one
produced confident nonsense rather than an error:

  * It used Theme.qml's BT.601 `luma()` for the contrast ratio. WCAG is defined
    on LINEARISED relative luminance. Using the wrong one understated every
    ratio on a dark background and reported gruvbox's own text at 4.4:1 when it
    is over 8:1.
  * Its HLS-lightness solver disagreed with itself about a single colour and
    returned values that had changed hue. Replaced with a monotonic lerp.
  * `resolve()` honoured explicit tokens only for names in RAMP_FRACTIONS, so
    every theme was measured with chadvis-dark's error/success/warning.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFINITIONS = ROOT / "src/qml/styles/ThemeDefinitions.qml"

# Theme.qml ramp() fractions. These MUST track src/qml/styles/Theme.qml; the
# table there says the same thing and a mismatch here is a false all-clear.
RAMP_FRACTIONS = {
    "backgroundAlt": 0.02,
    "surface": 0.05,
    "surfaceRaised": 0.085,
    "surfaceOverlay": 0.12,
    "border": 0.139,
    "borderLight": 0.209,
    "textDisabled": 0.305,
    "textSecondary": 0.48,
    "textPrimaryVariant": 0.712,
    "textPrimary": 0.864,
}

# Text tokens and the minimum ratio they must clear (WCAG 1.4.3).
TEXT_AA = 4.5
# Non-text UI: the accent as a focus ring or control outline (1.4.11).
UI_AA = 3.0

# How far past a threshold a suggested colour aims, so 8-bit rounding cannot
# push the stored hex back under it.
HEADROOM = 0.03

# Foreground -> the backgrounds it is actually painted on, worst case first, the
# minimum it has to clear, and whether it GATES.
#
# A pair marked decorative does not fail the run. It is still measured and
# printed, because an invisible border is a design problem even when it is not
# an accessibility one -- but a script that can never go green is not a gate.
#
#   textDisabled — WCAG 1.4.3 exempts text that is part of an inactive
#                  component. Disabled text has no minimum by definition, so
#                  holding it to 3:1 was this script inventing a requirement.
#   borderLight  — a card/panel separator. 1.4.11 exempts purely decorative
#                  borders; the functional outline in this app is borderFocus,
#                  which is the accent and IS gated below.
#   recording    — Theme.qml FIXED default, not a per-theme token, so a
#                  per-theme fix would be overwritten by the next theme. It is a
#                  genuine finding at ~2.2:1 on a raised surface and is tracked
#                  by T0119, which owns the fix to the shared semantic.
TEXT_PAIRS = [
    ("textPrimary", ["surfaceRaised", "surfaceOverlay", "surface", "background"], TEXT_AA, False),
    ("textPrimaryVariant", ["surfaceRaised", "surface"], TEXT_AA, False),
    ("textSecondary", ["surfaceRaised", "surface", "background"], TEXT_AA, False),
    ("textOnAccent", ["accent"], TEXT_AA, False),
    # The accent is used as borderFocus, which IS a functional control outline.
    ("textLink", ["surfaceRaised", "background"], TEXT_AA, False),
    ("accent", ["surface", "surfaceRaised", "background"], UI_AA, False),
    ("error", ["surfaceRaised", "background"], UI_AA, False),
    ("success", ["surfaceRaised", "background"], UI_AA, False),
    ("warning", ["surfaceRaised", "background"], UI_AA, False),
    ("textDisabled", ["surfaceRaised", "surface"], UI_AA, True),
    ("borderLight", ["background", "surface"], UI_AA, True),
    ("recording", ["surfaceRaised", "background"], UI_AA, True),
]

HEX = re.compile(r'^\s*([A-Za-z][A-Za-z0-9]*)\s*:\s*"(#[0-9A-Fa-f]{8})"')


def parse_definitions(text: str) -> dict[str, dict[str, str]]:
    """Pull `id -> colors` out of the QML source.

    Parses the literal rather than importing it: QML is not Python, and the
    point of this script is to run before anything builds.
    """
    body = text.split("readonly property var builtin", 1)[1]
    themes: dict[str, dict[str, str]] = {}
    current: str | None = None
    in_colors = False
    for raw in body.splitlines():
        line = raw.strip()
        theme = re.match(r'^"([a-z0-9-]+)"\s*:\s*\{', line)
        if theme:
            current = theme.group(1)
            themes[current] = {}
            in_colors = False
            continue
        if line.startswith("colors:"):
            in_colors = True
            continue
        if in_colors and line == "}":
            in_colors = False
            continue
        if in_colors and current:
            found = HEX.match(line)
            if found:
                themes[current][found.group(1)] = found.group(2).upper()
    return themes


def rgb(hex8: str) -> tuple[float, float, float]:
    """#RRGGBBAA, positional. NOT #AARRGGBB -- that is the whole point (T0047)."""
    return tuple(int(hex8[i : i + 2], 16) / 255.0 for i in (1, 3, 5))


def luma(colour: tuple[float, float, float]) -> float:
    """ITU-R BT.601, matching Theme.qml's luma().

    This decides which way a ramp travels and which of accent/background counts
    as "light" in the UI. It is NOT the quantity WCAG contrast is defined in.
    """
    r, g, b = colour
    return 0.299 * r + 0.587 * g + 0.114 * b


def relative_luminance(colour: tuple[float, float, float]) -> float:
    """WCAG 2.x relative luminance: linearize each channel, then weight.

    This is a DIFFERENT function from luma() and conflating the two is the
    specific mistake this script was first written with -- BT.601 weights on
    gamma-encoded values, which flattens the curve and understates every ratio
    on a dark background. Any script that reports "gruvbox's own text is 4.4:1"
    is measuring the wrong thing; against #282828 it is over 8:1.
    """

    def linearise(channel: float) -> float:
        return channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4

    r, g, b = (linearise(c) for c in colour)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ramp(base: tuple[float, float, float], fraction: float) -> tuple[float, float, float]:
    """Theme.qml's ramp(): move each channel a fraction toward the opposite pole."""
    toward = 0.0 if luma(base) > 0.5 else 1.0
    out = []
    for channel in base:
        shift = channel + (toward - channel) * fraction
        out.append(min(1.0, max(0.0, shift)))
    return tuple(out)


def contrast(fg: tuple[float, float, float], bg: tuple[float, float, float]) -> float:
    """WCAG 2.x contrast ratio, (L1+0.05)/(L2+0.05)."""
    lf, lb = relative_luminance(fg), relative_luminance(bg)
    if lf < lb:
        lf, lb = lb, lf
    return (lf + 0.05) / (lb + 0.05)


def resolve(tokens: dict[str, str]) -> dict[str, tuple[float, float, float]]:
    """Apply an explicit token if given, else derive it from accent/background."""
    out: dict[str, tuple[float, float, float]] = {}
    # The two mutable bases come straight from the definition; everything below
    # is either explicit in the definition or ramped off these two.
    for base in ("accent", "background"):
        if base in tokens:
            out[base] = rgb(tokens[base])

    for name, fraction in RAMP_FRACTIONS.items():
        if name in tokens:
            out[name] = rgb(tokens[name])
            continue
        source = "accent" if name == "textLink" else "background"
        if source not in out:
            continue
        out[name] = ramp(out[source], fraction)

    # Any token the definition names explicitly wins, derived or not.
    #
    # This used to special-case only textLink, and that was not enough: every
    # explicit token that is not in RAMP_FRACTIONS -- error, success, warning,
    # textOnAccent -- was ignored and then filled in from FIXED, so nord was
    # measured with chadvis-dark's red and gruvbox with chadvis-dark's red.
    # The measured failure belonged to the script, not the palette.
    for name, value in tokens.items():
        out[name] = rgb(value)

    if "textLink" not in out and "accent" in out:
        out["textLink"] = out["accent"]
    return out


# Semantics and recording have no ramp -- Theme.qml's fixed defaults, carried here
# so a theme cannot silently improve them and no theme can make them worse.
FIXED = {
    "success": rgb("#00FF88FF"),
    "warning": rgb("#FFB300FF"),
    "error": rgb("#FF4444FF"),
    "recording": rgb("#FF0000FF"),
    "recordingPulse": rgb("#FF3333FF"),
    "info": rgb("#00BCD4FF"),
}


def suggest(token_colour, backgrounds, minimum):
    """Nearest colour that clears `minimum`, so a fix is measured not guessed.

    Searches two directions -- lerping toward white and toward black -- and
    returns whichever clears first, which makes the smallest change that works
    while preserving hue and saturation far better than re-deriving the token.

    `HEADROOM` is not optional. A solution landing exactly on the threshold is
    then rounded to two hex digits and drops back UNDER it, which is how this
    first produced a "fixed" value the very next run called broken. Aim past the
    target and the rounding has somewhere to fall.

    This was originally an HLS-lightness search. That was wrong: `rgb_to_hls`
    and a hand-computed lightness disagreed about the SAME colour, and it
    reported a value that had shifted hue, which is worse than no suggestion.
    A lerp along a straight line in RGB space is monotonic in contrast against
    a fixed background, so it cannot do that. Returns None if neither end of
    the range clears -- which means the *background* is the thing to fix.
    """
    # WORST background, not the best one. Taking the max here returns the token
    # unchanged whenever it happens to clear on one surface, which is how the
    # first version of this function cheerfully suggested no change at all.
    worst = min(contrast(token_colour, bg) for bg in backgrounds)
    if worst >= minimum:
        return token_colour

    target = minimum * (1.0 + HEADROOM)

    def lerp(amount: float, towards=(1.0, 1.0, 1.0)):
        return tuple(c + (towards[i] - c) * amount for i, c in enumerate(token_colour))

    for towards in ((1.0, 1.0, 1.0), (0.0, 0.0, 0.0)):
        for step in range(1, 1001):
            candidate = lerp(step / 1000.0, towards)
            if all(contrast(candidate, bg) >= target for bg in backgrounds):
                return candidate
    return None


def to_hex(colour: tuple[float, float, float]) -> str:
    return "#" + "".join(f"{round(min(1.0, max(0.0, c)) * 255):02X}" for c in colour) + "FF"


def main() -> int:
    if not DEFINITIONS.exists():
        print(f"missing {DEFINITIONS}", file=sys.stderr)
        return 2

    themes = parse_definitions(DEFINITIONS.read_text())
    if not themes:
        print("parsed no themes -- the QML shape changed, fix this parser", file=sys.stderr)
        return 2

    failures: list[str] = []
    fixes: dict[str, dict[str, str]] = {}
    for theme_id, tokens in themes.items():
        resolved = resolve(tokens)
        # FALLBACK, not an override. Theme.qml lets a definition name its own
        # success/warning/error; these are only the values for a theme that
        # does not. `update()` here overwrote every theme's own palette with the
        # ChadVis defaults and made three themes look broken at once.
        for name, colour in FIXED.items():
            resolved.setdefault(name, colour)
        if "textPrimary" not in resolved or "accent" not in resolved:
            failures.append(f"{theme_id}: no accent or textPrimary resolved")
            continue

        print(f"\n{theme_id}")
        print(f"  {'token':<20} {'on':<18} {'ratio':>7}  {'need':>5}  ")
        for name, backgrounds, minimum, decorative in TEXT_PAIRS:
            if name not in resolved:
                continue
            for background in backgrounds:
                if background not in resolved:
                    continue
                ratio = contrast(resolved[name], resolved[background])
                ok = ratio >= minimum
                marker = " " if ok else ("~" if decorative else "<")
                print(f"  {name:<20} {background:<18} {ratio:>6.2f}:1 {minimum:>5.1f} {marker}")
                if not ok and not decorative:
                    failures.append(
                        f"{theme_id}: {name} on {background} is {ratio:.2f}:1, needs {minimum:.1f}:1"
                    )
                    if "--suggest" in sys.argv and name in tokens:
                        candidate = suggest(
                            resolved[name],
                            [resolved[b] for b in backgrounds if b in resolved],
                            minimum,
                        )
                        if candidate:
                            fixes.setdefault(theme_id, {})[name] = to_hex(candidate)

    if fixes:
        print("\nsuggested (lightness only, hue and saturation preserved):")
        for theme_id, entries in fixes.items():
            for name, value in entries.items():
                print(f"  {theme_id}: {name}: \"{value}\",")

    print()
    if failures:
        print(f"{len(failures)} failing pair(s):")
        for line in failures:
            print(f"  - {line}")
        return 1
    print("all measured pairs clear their WCAG minimum")
    return 0


if __name__ == "__main__":
    sys.exit(main())