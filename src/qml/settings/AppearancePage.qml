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

        Text {
            Layout.fillWidth: true
            text: "Choose the accent and background palette used throughout ChadVis."
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
