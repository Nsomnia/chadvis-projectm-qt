/**
* @file Theme.qml
* @brief Theme singleton providing consistent styling across QML components
*
* Defines color palette, metrics, and typography for the modern UI.
* Supports runtime theme switching via ThemeManager.
*
* @version 1.0.0
*/

pragma Singleton
import QtQuick

QtObject {
    id: theme

    // ═══════════════════════════════════════════════════════════
    // PRIMARY PALETTE - Cyan accent system
    // ═══════════════════════════════════════════════════════════

    readonly property color accent: "#00bcd4"
    readonly property color accentHover: "#26c6da"
    readonly property color accentPressed: "#0097a7"
    readonly property color accentLight: "#80deea"
    readonly property color accentDark: "#00838f"

    // ═══════════════════════════════════════════════════════════
    // BACKGROUND COLORS - Dark theme base
    // ═══════════════════════════════════════════════════════════

    readonly property color background: "#1a1a1a"
    readonly property color backgroundAlt: "#1e1e1e"
    readonly property color surface: "#252525"
    readonly property color surfaceRaised: "#2d2d2d"
    readonly property color surfaceOverlay: "#353535"

    // Glassmorphism backgrounds (semi-transparent)
    readonly property color glassBackground: Qt.rgba(0.164, 0.164, 0.164, 0.85)
    readonly property color glassBorder: Qt.rgba(0, 0.733, 0.831, 0.3)
    readonly property color glassHighlight: Qt.rgba(0, 0.733, 0.831, 0.1)

    // ═══════════════════════════════════════════════════════════
    // TEXT COLORS
    // ═══════════════════════════════════════════════════════════

    readonly property color textPrimary: "#e0e0e0"
    readonly property color textSecondary: "#888888"
    readonly property color textDisabled: "#606060"
    readonly property color textOnAccent: "#1a1a1a"
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

    readonly property color border: "#3a3a3a"
    readonly property color borderLight: "#4a4a4a"
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

    readonly property string fontFamily: "Inter, \"Noto Sans\", sans-serif"

    readonly property font fontTiny: Qt.font({
        family: fontFamily,
        pixelSize: 10,
        weight: Font.Normal
    })

    readonly property font fontCaption: Qt.font({
        family: fontFamily,
        pixelSize: 12,
        weight: Font.Normal
    })

    readonly property font fontBody: Qt.font({
        family: fontFamily,
        pixelSize: 14,
        weight: Font.Normal
    })

    readonly property font fontBodyStrong: Qt.font({
        family: fontFamily,
        pixelSize: 14,
        weight: Font.DemiBold
    })

    readonly property font fontSubtitle: Qt.font({
        family: fontFamily,
        pixelSize: 16,
        weight: Font.DemiBold
    })

    readonly property font fontTitle: Qt.font({
        family: fontFamily,
        pixelSize: 18,
        weight: Font.Bold
    })

    readonly property font fontHeading: Qt.font({
        family: fontFamily,
        pixelSize: 24,
        weight: Font.Bold
    })

    readonly property font fontDisplay: Qt.font({
        family: fontFamily,
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

    // Glow for accent elements
    readonly property var glowAccent: {
        "color": Qt.rgba(0, 0.733, 0.831, 0.5),
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
}
