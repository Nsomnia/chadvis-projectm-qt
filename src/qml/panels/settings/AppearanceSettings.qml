/**
 * @file AppearanceSettings.qml
 * @brief Settings section: accent/background colour preview and edit
 *
 * The colours live in the QML `Theme` singleton and are persisted through
 * `SettingsBridge.accentColor` / `SettingsBridge.backgroundColor`. Exposes
 * apply() so the parent panel's "Save & Apply" action can commit the edited
 * colours without reaching into internal control ids.
 *
 * T0047. This panel used to hold two `AppTextField`s whose `text` was bound to
 * `Theme.accent` — a `color` assigned to a `string` field, which qmllint
 * reports as `incompatible-type` and which yields a coerced string, not the
 * hex an editor needs. They are gone: the preview swatches read the colour
 * directly, and apply() round-trips the colour through Theme's own hex
 * conversions, so a value the user actually picked is what gets persisted.
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

    function apply() {
        // Theme.toRgbaHex() -> "#RRGGBBAA", the byte order vc::Color reads.
        // Theme.applyAccent/applyBackground then split it back into components
        // and write it to config.toml through SettingsBridge.
        //
        // Going through the Theme mutators rather than writing SettingsBridge
        // directly is what keeps the derived variants (accentHover, surface,
        // overlay, ...) recomputed from the new colour -- writing the bridge
        // field alone would leave the whole palette showing the old colour.
        Theme.applyAccent(Theme.toRgbaHex(Theme.accent))
        Theme.applyBackground(Theme.toRgbaHex(Theme.background))
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.spacingMedium
        rowSpacing: Theme.spacingSmall

        Text { text: "Accent"; color: Theme.textSecondary; font: Theme.fontCaption }
        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                width: 20
                height: 20
                radius: 4
                color: Theme.accent
                border.color: Theme.border
            }
            Text {
                Layout.fillWidth: true
                text: Theme.toRgbaHex(Theme.accent)
                color: Theme.textSecondary
                font: Theme.fontCaption
                elide: Text.ElideRight
            }
        }

        Text { text: "Background"; color: Theme.textSecondary; font: Theme.fontCaption }
        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                width: 20
                height: 20
                radius: 4
                color: Theme.background
                border.color: Theme.border
            }
            Text {
                Layout.fillWidth: true
                text: Theme.toRgbaHex(Theme.background)
                color: Theme.textSecondary
                font: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: "Changes are saved automatically when you apply them."
        color: Theme.textDisabled
        font: Theme.fontCaption
    }
}
