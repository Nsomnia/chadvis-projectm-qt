/**
 * @file AppearanceSettings.qml
 * @brief Settings section: accent/background colour editing
 *
 * The colours live in the QML `Theme` singleton and are persisted through
 * `SettingsBridge.accentColor` / `SettingsBridge.backgroundColor`. Exposes
 * apply() so the parent panel's "Save & Apply" action can re-commit the colours
 * without reaching into internal control ids.
 *
 * T0183. This panel had no editing control of any kind -- two read-only
 * swatches and two read-only hex labels -- so "clicking a colour did nothing".
 * The cause was T0047's fix (866b36e), which resolved a qmllint
 * `incompatible-type` by deleting the two `AppTextField`s rather than by
 * retyping them. Both halves are back, and neither reintroduces that error:
 *
 *  - `AppColorPicker` supplies the hue/saturation/value/alpha UI.
 *  - The hex fields are bound string-to-string, `Theme.toRgbaHex(color)` in,
 *    never `Theme.accent` (a `color`) into `text` (a `string`).
 *
 * The hex fields are NOT declaratively bound to a live expression. A TextField
 * whose `text` is bound loses the binding the moment the user types, and QML
 * offers no way to restore it imperatively, so the field would silently stop
 * tracking Theme. They are seeded once and then kept current by a Connections
 * handler on Theme.themeChanged().
 *
 * @version 1.1.0
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    id: root
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Appearance"
    }

    // "#RRGGBB" or "#RRGGBBAA", with the '#' optional on entry.
    readonly property var hexValidator: /^\s*#?[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?\s*$/

    function normalizeHex(text) {
        const trimmed = String(text).trim()
        return trimmed.charAt(0) === "#" ? trimmed : ("#" + trimmed)
    }

    function apply() {
        // Theme.toRgbaHex() -> "#RRGGBBAA", the byte order vc::Color reads.
        // Theme.applyAccent/applyBackground then split it back into components
        // and write it to config.toml through SettingsBridge.
        //
        // Going through the Theme mutators rather than writing SettingsBridge
        // directly is what keeps the derived tokens (accentHover, surface,
        // border, textPrimary, glassBorder, ...) recomputed from the new
        // colour -- writing the bridge field alone would leave the palette
        // showing the old colour.
        Theme.applyAccent(Theme.toRgbaHex(Theme.accent))
        Theme.applyBackground(Theme.toRgbaHex(Theme.background))
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: Theme.spacingMedium
        rowSpacing: Theme.spacingMedium

        Text {
            text: "Accent"
            color: Theme.textSecondary
            font: Theme.fontCaption
            Layout.alignment: Qt.AlignVCenter
        }

        AppColorPicker {
            id: accentPicker
            value: Theme.accent
            onPicked: function (c) { Theme.applyAccent(c) }
            Layout.alignment: Qt.AlignVCenter
        }

        AppTextField {
            id: accentHex
            Layout.fillWidth: true
            text: Theme.toRgbaHex(Theme.accent)
            font: Theme.fontCaption
            validator: RegExpValidator { regularExpression: root.hexValidator }
            selectByMouse: true
            onEditingFinished: {
                // Accept 6- or 8-digit, with or without the '#'. applyAccent
                // re-parses positionally, so the byte order is never Qt's to
                // guess at -- that was the whole of T0047's second half.
                Theme.applyAccent(root.normalizeHex(text))
                // Normalise the display to "#RRGGBBAA" whatever was typed.
                text = Theme.toRgbaHex(Theme.accent)
            }
        }

        Text {
            text: "Background"
            color: Theme.textSecondary
            font: Theme.fontCaption
            Layout.alignment: Qt.AlignVCenter
        }

        AppColorPicker {
            value: Theme.background
            onPicked: function (c) { Theme.applyBackground(c) }
            Layout.alignment: Qt.AlignVCenter
        }

        AppTextField {
            id: backgroundHex
            Layout.fillWidth: true
            text: Theme.toRgbaHex(Theme.background)
            font: Theme.fontCaption
            validator: RegExpValidator { regularExpression: root.hexValidator }
            selectByMouse: true
            onEditingFinished: {
                Theme.applyBackground(root.normalizeHex(text))
                text = Theme.toRgbaHex(Theme.background)
            }
        }
    }

    // Keeps both hex fields showing the truth after a picker drag, and after
    // anything else that calls applyAccent/applyBackground.
    Connections {
        target: Theme
        function onThemeChanged() {
            accentHex.text = Theme.toRgbaHex(Theme.accent)
            backgroundHex.text = Theme.toRgbaHex(Theme.background)
        }
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: "Colours apply immediately and are written to the config on exit. "
              + "Hex accepts #RRGGBB or #RRGGBBAA; the alpha byte is last, not first."
        color: Theme.textDisabled
        font: Theme.fontCaption
    }
}