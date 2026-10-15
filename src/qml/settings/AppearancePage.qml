import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../panels/settings"

Flickable {
    id: root

    contentHeight: pageLayout.implicitHeight + Theme.spacingXL * 2
    clip: true

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    ColumnLayout {
        id: pageLayout
        x: Theme.spacingLarge
        y: Theme.spacingLarge
        width: root.width - Theme.spacingLarge * 2
        spacing: Theme.spacingMedium

        Text {
            text: "Appearance"
            color: Theme.textPrimary
            font: Theme.fontHeading
        }

        // T0190. Was "Choose the accent and background palette used throughout
        // ChadVis." That described a panel with one editable field; it now
        // describes a theme. Naming the built-ins here rather than leaving the
        // user to discover them in the combo is the point -- four named themes
        // is the discoverable feature. Kept as a literal rather than built from
        // Theme.availableThemeIds(): the set of built-ins changes when the
        // catalogue grows, and a stale sentence in a settings page is a smaller
        // failure than a binding that fails silently. T0190 covers the gap.
        Text {
            Layout.fillWidth: true
            text: "Pick a built-in theme — Material, ChadVis Dark, Gruvbox or Nord — "
                  + "or set a custom accent and background of your own. Choosing "
                  + "either changes every colour in the window, not just the panel."
            color: Theme.textSecondary
            font: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        AppearanceSettings {
            id: appearanceSettings
            Layout.fillWidth: true
        }
    }

    function apply() {
        appearanceSettings.apply()
    }
}
