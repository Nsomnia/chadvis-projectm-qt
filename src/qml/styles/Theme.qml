/**
 * @file Theme.qml
 * @brief Theme singleton providing consistent styling across QML components
 *
 * Defines color palette, metrics, and typography for the modern UI.
 * Runtime theme switching happens through this singleton: applyAccent() and
 * applyBackground() mutate the two base colors and every derived token
 * re-evaluates. There is no ThemeManager -- the line this replaces named a
 * class that never existed in any of the 554 commits up to T0185.
 *
 * Mutable: accent + background (the two user-customizable bases)
 * Derived: surfaces, glass, text ramp, borders, glow -- bindings on the bases
 * Fixed:   metrics, typography, shadows, and the semantic colors T0119 owns
 *
 * @version 1.1.0
 */

pragma Singleton
import QtQuick
// applyAccent/applyBackground write the persisted colours through SettingsBridge,
// which lives in this module. A singleton in a module may import its own URI.
import ChadVis

QtObject {
    id: theme

    // ═══════════════════════════════════════════════════════════
    // SIGNALS
    // ═══════════════════════════════════════════════════════════

    /** Emitted when any mutable theme property changes (accent or background family) */
    signal themeChanged()

    // ═══════════════════════════════════════════════════════════
    // PRIMARY PALETTE - Cyan accent system (MUTABLE)
    // ═══════════════════════════════════════════════════════════

    property color accent: "#00bcd4"
    property color accentHover: "#26c6da"
    property color accentPressed: "#0097a7"
    property color accentLight: "#80deea"
    property color accentDark: "#00838f"

    // ═══════════════════════════════════════════════════════════
    // COLOUR DERIVATION
    //
    // Every token below is DERIVED from exactly two mutable inputs: `accent`
    // and `background`. That is the whole point. T0185: `glassBorder`,
    // `glassHighlight` and `glowAccent` hardcoded the shipped cyan, and
    // `border`, `borderLight` and `textDisabled` were `readonly` at their dark
    // values -- so a user who picked an accent got cyan glass, and a user who
    // picked a background got unreadable text, with no way to tell the picker
    // from the bug. T0118 recorded the same defect from the light-mode side.
    //
    // The ramp is RELATIVE, not multiplicative. `Qt.lighter(black, 2)` is still
    // black, so the old `Qt.lighter` chain collapsed to a flat black UI at the
    // value actually in config.toml (`visualizer_background = '#000000FF'`).
    // Moving toward the opposite pole by a fraction of the distance that
    // remains produces a usable ramp from any base, black included, and keeps
    // the hand-tuned proportions of the dark defaults.
    //
    // The fractions below are the dark defaults expressed that way, each fitted
    // against `#1a1a1a` so the dark theme renders as it did:
    //
    //   background #1a1a1a   surface #252525   raised #2d2d2d   overlay #353535
    //   border     #3a3a3a   text    #e0e0e0   secondary #888888  disabled #606060
    //
    // ═══════════════════════════════════════════════════════════

    /// Perceived brightness, 0..1. ITU-R BT.601 luma.
    function luma(c) {
        return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b
    }

    /// Move `base` toward the opposite pole (white on a dark base, black on a
    /// light one) by `fraction` of the distance remaining, per channel so a
    /// tinted background ramps along its own hue. Alpha is carried through.
    function ramp(base, fraction) {
        const toward = luma(base) > 0.5 ? 0.0 : 1.0
        const shift = function(c) {
            const moved = c + (toward - c) * fraction
            return Math.max(0, Math.min(1, moved))
        }
        return Qt.rgba(shift(base.r), shift(base.g), shift(base.b), base.a)
    }

    /// True when the background is light enough that text on it must be dark.
    /// This is the branch T0118 needs and does not have.
    readonly property bool backgroundIsLight: luma(background) > 0.5

    /// True when the accent is light enough that text on it must be dark. The
    /// shipped `#00bcd4` cyan lands just above the threshold, so the default
    /// textOnAccent stays dark exactly as it was hardcoded.
    readonly property bool accentIsLight: luma(accent) > 0.5

    // ═══════════════════════════════════════════════════════════
    // BACKGROUND COLORS (MUTABLE base, everything else derived)
    // ═══════════════════════════════════════════════════════════

    property color background: "#1a1a1a"
    readonly property color backgroundAlt: ramp(background, 0.02)
    readonly property color surface: ramp(background, 0.05)
    readonly property color surfaceRaised: ramp(background, 0.085)
    readonly property color surfaceOverlay: ramp(background, 0.12)

    // Glassmorphism backgrounds (semi-transparent). The base followed a
    // hardcoded neutral grey and both tints followed the hardcoded cyan; all
    // three now follow the surface and the accent.
    readonly property color glassBackground: Qt.rgba(surface.r, surface.g, surface.b, 0.85)
    readonly property color glassBorder: Qt.rgba(accent.r, accent.g, accent.b, 0.30)
    readonly property color glassHighlight: Qt.rgba(accent.r, accent.g, accent.b, 0.10)

    // ═══════════════════════════════════════════════════════════
    // TEXT COLORS
    // ═══════════════════════════════════════════════════════════

    readonly property color textPrimary: ramp(background, 0.864)
    // Softened primary for large quiet surfaces (was referenced by panels but
    // never defined — LyricsPanel/OverlayPanel used it as an implicit undefined).
    // T0119 measured this at 6.53-9.26:1 and says leave it alone; the fraction
    // below reproduces #bdbdbd exactly at the dark base, so it is untouched.
    readonly property color textPrimaryVariant: ramp(background, 0.712)
    readonly property color textSecondary: ramp(background, 0.480)
    readonly property color textDisabled: ramp(background, 0.305)
    // Follows the ACCENT, not the background: a light accent needs dark text.
    readonly property color textOnAccent: accentIsLight ? "#1a1a1a" : ramp(accent, 0.88)
    readonly property color textLink: accent

    // ═══════════════════════════════════════════════════════════
    // SEMANTIC COLORS
    // ═══════════════════════════════════════════════════════════

    readonly property color success: "#00ff88"
    readonly property color successDim: "#00cc6a"
    readonly property color warning: "#ffb300"
    readonly property color warningDim: "#cc8f00"
    readonly property color error: "#ff4444"
    readonly property color errorDim: "#cc3535"
    readonly property color info: accent

    // Recording indicator
    readonly property color recording: "#ff0000"
    readonly property color recordingPulse: "#ff3333"

    // ═══════════════════════════════════════════════════════════
    // BORDER COLORS
    // ═══════════════════════════════════════════════════════════

    readonly property color border: ramp(background, 0.139)
    readonly property color borderLight: ramp(background, 0.209)
    readonly property color borderFocus: accent

    // ═══════════════════════════════════════════════════════════
    // METRICS - Spacing
    // ═══════════════════════════════════════════════════════════

    readonly property int spacingTiny: 4
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 16
    readonly property int spacingLarge: 24
    readonly property int spacingXL: 32

    // ═══════════════════════════════════════════════════════════
    // METRICS - Border Radius
    // ═══════════════════════════════════════════════════════════

    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12
    readonly property int radiusXL: 16
    readonly property int radiusRound: 999

    // ═══════════════════════════════════════════════════════════
    // METRICS - Sizes
    // ═══════════════════════════════════════════════════════════

    readonly property int iconSmall: 16
    readonly property int iconMedium: 24
    readonly property int iconLarge: 32
    readonly property int iconXL: 48

    readonly property int buttonHeight: 36
    readonly property int buttonHeightLarge: 48
    readonly property int inputHeight: 36

    readonly property int sidebarCollapsedWidth: 60
    readonly property int sidebarExpandedWidth: 280

    // Nav rail (app shell) — collapsed = icons only, expanded = icon + label
    readonly property int navRailWidthCollapsed: 64
    readonly property int navRailWidthExpanded: 224
    // Window width below which the rail force-collapses regardless of preference
    readonly property int navRailAutoCollapseBelow: 1000

    // Library grid — minimum tile width before columns reflow
    readonly property int cardTileMinimum: 220

    readonly property int panelHeaderHeight: 40
    readonly property int transportHeight: 56
    readonly property int topBarHeight: 40
    readonly property int statusBarHeight: 28

    // ═══════════════════════════════════════════════════════════
    // ANIMATION DURATIONS
    // ═══════════════════════════════════════════════════════════

    readonly property int durationInstant: 50
    readonly property int durationFast: 150
    readonly property int durationNormal: 250
    readonly property int durationSlow: 400
    readonly property int durationVerySlow: 600

    // ═══════════════════════════════════════════════════════════
    // TYPOGRAPHY
    // ═══════════════════════════════════════════════════════════

    // QFont takes a real family list; passing a comma-delimited string to
    // font.family makes Qt search for that entire (missing) family name.
    // Helvetica Neue is the macOS primary; concrete platform fonts follow.
    readonly property string fontFamily: "Helvetica Neue"
    readonly property var fontFamilies: [
        "Helvetica Neue",
        "Segoe UI",
        "Noto Sans",
        "DejaVu Sans",
        "Arial"
    ]

    readonly property font fontTiny: Qt.font({
        families: fontFamilies,
        pixelSize: 10,
        weight: Font.Normal
    })

    readonly property font fontCaption: Qt.font({
        families: fontFamilies,
        pixelSize: 12,
        weight: Font.Normal
    })

    readonly property font fontCaptionStrong: Qt.font({
        families: fontFamilies,
        pixelSize: 12,
        weight: Font.DemiBold
    })

    readonly property font fontBody: Qt.font({
        families: fontFamilies,
        pixelSize: 14,
        weight: Font.Normal
    })

    readonly property font fontBodyStrong: Qt.font({
        families: fontFamilies,
        pixelSize: 14,
        weight: Font.DemiBold
    })

    readonly property font fontSubtitle: Qt.font({
        families: fontFamilies,
        pixelSize: 16,
        weight: Font.DemiBold
    })

    readonly property font fontTitle: Qt.font({
        families: fontFamilies,
        pixelSize: 18,
        weight: Font.Bold
    })

    readonly property font fontHeading: Qt.font({
        families: fontFamilies,
        pixelSize: 24,
        weight: Font.Bold
    })

    readonly property font fontDisplay: Qt.font({
        families: fontFamilies,
        pixelSize: 32,
        weight: Font.Bold
    })

    // ═══════════════════════════════════════════════════════════
    // SHADOWS
    // ═══════════════════════════════════════════════════════════

    readonly property var shadowSmall: {
        "color": Qt.rgba(0, 0, 0, 0.2),
        "horizontalOffset": 0,
        "verticalOffset": 2,
        "radius": 4
    }

    readonly property var shadowMedium: {
        "color": Qt.rgba(0, 0, 0, 0.3),
        "horizontalOffset": 0,
        "verticalOffset": 4,
        "radius": 8
    }

    readonly property var shadowLarge: {
        "color": Qt.rgba(0, 0, 0, 0.4),
        "horizontalOffset": 0,
        "verticalOffset": 8,
        "radius": 16
    }

    // Glow for accent elements. T0185: the colour was the hardcoded shipped
    // cyan, so a picked accent left every glow the old default's.
    readonly property var glowAccent: {
        "color": Qt.rgba(accent.r, accent.g, accent.b, 0.5),
        "horizontalOffset": 0,
        "verticalOffset": 0,
        "radius": 12
    }

    // ═══════════════════════════════════════════════════════════
    // HELPER FUNCTIONS
    // ═══════════════════════════════════════════════════════════

    function formatTime(ms) {
        if (!ms || ms < 0) return "0:00"
        var totalSec = Math.floor(ms / 1000)
        var min = Math.floor(totalSec / 60)
        var sec = totalSec % 60
        return min + ":" + (sec < 10 ? "0" : "") + sec
    }

    function formatTimeDetailed(ms) {
        if (!ms || ms < 0) return "0:00:00"
        var totalSec = Math.floor(ms / 1000)
        var hr = Math.floor(totalSec / 3600)
        var min = Math.floor((totalSec % 3600) / 60)
        var sec = totalSec % 60
        if (hr > 0) {
            return hr + ":" + (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
        }
        return min + ":" + (sec < 10 ? "0" : "") + sec
    }

    function lighten(color, amount) {
        return Qt.lighter(color, 1 + amount)
    }

    function darken(color, amount) {
        return Qt.darker(color, 1 + amount)
    }

    function withAlpha(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    // ═══════════════════════════════════════════════════════════
    // RUNTIME THEME MUTATORS
    // ═══════════════════════════════════════════════════════════

    // Seeded from main.qml's Component.onCompleted, NOT from here.
    //
    // T0047 first seeded from this singleton's own Component.onCompleted and it
    // did not take: a QtObject singleton in a qt_add_qml_module is instantiated
    // lazily at first use, and its completion is not ordered after SettingsBridge's
    // construction, so the accent landed on the config default only sometimes --
    // the visible symptom was a stale blue accent from an earlier session
    // persisting while the persisted value said otherwise. main.qml is the
    // component that already reads SettingsBridge at completion, so seeding from
    // there is correctly ordered by construction.

    /// #RRGGBBAA -> a real Qt colour.
    ///
    /// This is the whole of the T0047 fix's second half. vc::Color writes its
    /// hex as `#RRGGBBAA`, but Qt parses an 8-digit hex as `#AARRGGBB` -- so
    /// passing the string through `Qt.color()` reads the ALPHA out of the RED
    /// channel and the RED out of the alpha. `#000000FF` (config's black, fully
    /// opaque) became rgba(0,0,0,0): a fully transparent window. On a desktop
    /// with a blue-toned wallpaper the whole UI then reads as mid blue, because
    /// what you are seeing is the wallpaper and nothing else. `#00FF88FF`
    /// (green, opaque) became rgba(255,136,0,0) -- transparent orange.
    ///
    /// Parsing by position, rather than trusting a hex parser's byte order, is
    /// the only way this can be correct for both a 6-digit and an 8-digit value.
    function parseRgbaHex(value) {
        const text = String(value).trim()
        if (text.length === 0 || text.charAt(0) !== "#")
            return Qt.rgba(0, 0, 0, 1)
        const digits = text.substring(1)
        if (digits.length < 6)
            return Qt.rgba(0, 0, 0, 1)
        const channel = function(offset) {
            return parseInt(digits.substring(offset, offset + 2), 16) / 255
        }
        const r = channel(0)
        const g = channel(2)
        const b = channel(4)
        // The 7th and 8th digits are the alpha when present. A 6-digit colour is
        // opaque by definition, which is what Qt means by omitting it.
        const a = digits.length >= 8 ? channel(6) : 1.0
        return Qt.rgba(r, g, b, a)
    }

    /// The inverse of parseRgbaHex, and the reason the pair exists.
    ///
    /// Emits "#RRGGBBAA" -- the byte order vc::Color::toHex writes and
    /// fromHex reads. A Qt color's own toString() emits "#AARRGGBB" when it has
    /// an alpha, so using it as the write-back string would round-trip the alpha
    /// into the red channel and back, which is a corruption that survives a save
    /// and reload rather than being a transient display artefact.
    function toRgbaHex(c) {
        const hex2 = function(v) {
            const s = Math.max(0, Math.min(255, Math.round(v * 255))).toString(16)
            return s.length === 1 ? "0" + s : s
        }
        return ("#" + hex2(c.r) + hex2(c.g) + hex2(c.b) + hex2(c.a)).toUpperCase()
    }

    /**
     * Apply a new accent color and recompute all derived accent variants.
     *
     * @param baseColor - The new accent color (#rrggbb or #rrggbbaa)
     */
    function applyAccent(baseColor) {
        const resolved = parseRgbaHex(baseColor)
        accent = resolved
        accentHover = Qt.lighter(resolved, 1.15)
        accentPressed = Qt.darker(resolved, 1.2)
        accentLight = Qt.lighter(resolved, 1.5)
        accentDark = Qt.darker(resolved, 1.5)
        // Written as "#rrggbb" because the bridge speaks hex text, not color.
        // toString() on a Qt.lighter() result can carry the alpha channel, and
        // vc::Color accepts 8 digits, so spelling the round trip out keeps a
        // future themed-with-opacity accent from silently losing its alpha.
        // Written back as "#RRGGBBAA", the byte order vc::Color expects. Not
        // Qt.color(x).toString(): if baseColor arrived as an 8-digit string Qt
        // has already mis-parsed it (#AARRGGBB), and writing the string back
        // would persist the mangled value into config.toml -- so the corruption
        // would survive the restart that was supposed to fix it.
        SettingsBridge.accentColor = toRgbaHex(resolved)
        themeChanged()
    }

    /**
     * Apply a new background color.
     *
     * Only the base is assigned. Every surface, border and text token is a
     * binding on `background` (see COLOUR DERIVATION above), so this sets one
     * value and the binding re-evaluates the ramp. The previous version
     * assigned surface/backgroundAlt/surfaceRaised/surfaceOverlay with
     * `Qt.lighter(resolved, 1.05..1.35)`, which is a multiplicative ramp: at
     * the `#000000` actually stored in config.toml it derived black from black
     * five times over and the UI flattened to a single shade.
     *
     * @param baseColor - The new background color (#rrggbb or #rrggbbaa)
     */
    function applyBackground(baseColor) {
        const resolved = parseRgbaHex(baseColor)
        background = resolved
        SettingsBridge.backgroundColor = toRgbaHex(resolved)
        themeChanged()
    }
}
