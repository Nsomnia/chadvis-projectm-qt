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

pragma Singleton

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
            description: "Gruvbox dark. Warm retro palette, muted teal accent.",
            isDark: true,
            author: "ChadVis (built-in)",
            colors: {
                // bg0/bg1/bg2/bg3. surfaceRaised is NOT bg2: gruvbox's own fg2
                // (#a89984) measures 3.17:1 on bg2, so text-bearing raised
                // controls would have shipped at a third of AA. This sits
                // between bg1 and bg2 instead and keeps the warm progression.
                // Measured by scripts/check-theme-contrast.py, not by eye.
                accent:          "#83A598FF",   // gruvbox fg4, not the dark #458588
                background:      "#282828FF",   // bg0
                surface:         "#3C3836FF",   // bg1
                surfaceRaised:   "#454039FF",   // between bg1 and bg2
                textPrimary:     "#EBDBB2FF",   // fg1
                textSecondary:   "#DCB378FF",   // between fg2 and fg1; fg2 fails AA on bg1
                textDisabled:    "#7C6F64FF",   // gray
                textOnAccent:    "#282828FF",   // bg0 on the light fg4
                // Links are TEXT and clear 4.5:1; the accent only has to clear
                // 3:1 as a focus ring. gruvbox fg4 is 3.81:1 on the raised
                // surface, so the link is a lighter step of the same teal.
                textLink:        "#98B4AAFF",
                success:         "#B8BB26FF",   // green
                warning:         "#FABD2FFF",   // yellow
                error:           "#FB5B48FF",   // red, 10% toward white (2.98:1 unmodified)
                border:          "#504945FF",   // bg2
                borderLight:     "#665C54FF"    // bg3
            }
        },

        "nord": {
            displayName: "Nord",
            description: "Nord. Cool arctic palette, frost blue accent.",
            isDark: true,
            author: "ChadVis (built-in)",
            colors: {
                // nord0 (#3b4252) is nord's own elevated surface; nord1
                // (#434c5e) is its polar-night highlight and frost2 sits at
                // 4.31:1 on it, just under AA for a link. So the raised step is
                // nord0 and nord1 is reserved for the overlay, where it does
                // not carry body text.
                accent:          "#88C0D0FF",   // frost2
                background:      "#2E3440FF",   // nord0 base
                surface:         "#3B4252FF",   // nord0
                surfaceRaised:   "#3B4252FF",   // nord0
                textPrimary:     "#ECEFF4FF",   // snow storm
                textSecondary:   "#D8DEE9FF",   // snow storm
                textDisabled:    "#7B88A1FF",   // frost3
                textOnAccent:    "#2E3440FF",   // nord0 on frost2
                // nord's aurora red is 2.46:1 on nord0 -- just under the 3:1 a
                // status colour needs. One step lighter, still unmistakably
                // aurora red. Measured, not eyeballed.
                error:           "#C97980FF",   // aurora red, 15% toward white
                success:         "#A3BE8CFF",   // aurora green
                warning:         "#EBCB8BFF",   // aurora yellow
                border:          "#4C566AFF",   // nord2
                borderLight:     "#5E6B85FF"    // nord3
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