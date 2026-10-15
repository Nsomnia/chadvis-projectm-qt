/**
 * @file ThemeDefinitions.qml
 * @brief Built-in theme definitions — data only, no logic.
 *
 * A theme is a *definition*: a named set of tokens. Adding a fifth built-in
 * must be adding one entry to `builtins` and one line to the Settings combo,
 * with no change to Theme.qml, SettingsBridge, ConfigParsers or the Settings
 * window. If adding a theme needs a code change, the engine is not finished.
 *
 * Storage note (T0190): docs/dev/THEME-ENGINE-PLAN.md §8.1 prefers JSON under
 * `resources/themes/` shipped through the Qt resource system, so that built-ins
 * and user files share one parser and one validator. QML cannot enumerate a
 * directory or read a file synchronously, so that storage needs the C++
 * ThemeCatalog — which does not exist yet. Until it does, the built-ins live
 * here, as the §8.1 fallback ("a QML data file for built-ins ... with the
 * catalog owning validation in both cases"). This file is the thing that moves
 * to JSON when the catalog lands; nothing else has to change when it does.
 *
 * Hex byte order: every value below is #RRGGBBAA. Qt parses an 8-digit hex as
 * #AARRGGBB, so a definition is never handed to Qt.color() directly — Theme.qml
 * runs every value through parseRgbaHex(). See THEME-ENGINE-PLAN.md §3.3.
 *
 * Token semantics: supply the base colours and any literal you care about.
 * Omitted tokens are derived by Theme.qml's ramp()/darker() rules or fall back
 * to the active definition's base — supplying accent + background alone yields a
 * complete, usable theme. See THEME-ENGINE-PLAN.md §4.1.
 *
 * Values are the proposed palettes from THEME-ENGINE-PLAN.md §5.1–5.4. They are
 * unreviewed against a real window: §16.5 requires a screenshot plus contrast
 * numbers before they ship, and T0191 tracks that.
 *
 * @version 1.0.0
 */

import QtQuick

QtObject {
    id: definitions

    /// The theme used for a fresh install. `dark` is a legacy alias that
    /// migrates to `chadvis-dark` so no existing user sees their look change
    /// (THEME-ENGINE-PLAN.md §7.2), which is why this is not `chadvis-dark`.
    readonly property string defaultId: "material"

    /// The last-resort palette, compiled in so that a corrupt resource cannot
    /// leave the window unpainted (§6.3). This is the old chadvis look.
    readonly property string fallbackId: "chadvis-dark"

    /// Ordered. The Settings combo shows this order, so put the ones a user is
    /// most likely to want first.
    readonly property var builtin: ({
        "material": {
            displayName: "Material",
            description: "Material 3 dark. Purple accent, neutral greys, coloured error.",
            isDark: true,
            author: "ChadVis (built-in)",
            colors: {
                accent:          "#BB86FCFF",
                background:      "#121212FF",
                surface:         "#1E1E1EFF",
                surfaceRaised:   "#2A2A2AFF",
                textPrimary:     "#E1E1E5FF",
                textSecondary:   "#9A9AA0FF",
                textDisabled:    "#7A7A7AFF",
                textOnAccent:    "#1A1A1AFF",
                success:         "#4CAF50FF",
                warning:         "#FFB300FF",
                error:           "#CF6679FF",
                border:          "#3A3A3AFF",
                borderLight:     "#4A4A4AFF"
            }
        },

        "chadvis-dark": {
            displayName: "ChadVis Dark",
            description: "The original ChadVis look. Green accent on black.",
            isDark: true,
            author: "ChadVis (built-in)",
            colors: {
                accent:          "#00FF88FF",
                background:      "#000000FF",
                surface:         "#0D0D0DFF",
                surfaceRaised:   "#161616FF",
                textPrimary:     "#E0E0E0FF",
                textSecondary:   "#888888FF",
                textDisabled:    "#606060FF",
                textOnAccent:    "#1A1A1AFF",
                success:         "#00FF88FF",
                warning:         "#FFB300FF",
                error:           "#FF4444FF",
                border:          "#232323FF",
                borderLight:     "#353535FF"
            }
        },

        "gruvbox": {
            displayName: "Gruvbox",
            description: "Gruvbox dark. Warm retro palette, muted blue accent.",
            isDark: true,
            author: "ChadVis (built-in)",
            colors: {
                accent:          "#458588FF",
                background:      "#282828FF",
                surface:         "#3C3836FF",
                surfaceRaised:   "#504945FF",
                textPrimary:     "#EBDBB2FF",
                textSecondary:   "#A89984FF",
                textDisabled:    "#7C6F64FF",
                textOnAccent:    "#282828FF",
                success:         "#B8BB26FF",
                warning:         "#FABD2FFF",
                error:           "#FB4934FF",
                border:          "#504945FF",
                borderLight:     "#665C54FF"
            }
        },

        "nord": {
            displayName: "Nord",
            description: "Nord. Cool arctic palette, frost blue accent.",
            isDark: true,
            author: "ChadVis (built-in)",
            colors: {
                accent:          "#88C0D0FF",
                background:      "#2E3440FF",
                surface:         "#3B4252FF",
                surfaceRaised:   "#434C5EFF",
                textPrimary:     "#ECEFF4FF",
                textSecondary:   "#D8DEE9FF",
                textDisabled:    "#7B88A1FF",
                textOnAccent:    "#2E3440FF",
                success:         "#A3BE8CFF",
                warning:         "#EBCB8BFF",
                error:           "#BF616AFF",
                border:          "#4C566AFF",
                borderLight:     "#5E6B85FF"
            }
        }
    })

    /// Theme ids in display order.
    function ids() {
        return Object.keys(builtin)
    }

    function has(id) {
        return id !== undefined && id !== null && Object.prototype.hasOwnProperty.call(builtin, id)
    }

    function definition(id) {
        return has(id) ? builtin[id] : undefined
    }
}