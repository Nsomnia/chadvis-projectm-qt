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
            text: "Profiles"
            color: Theme.textPrimary
            font: Theme.fontHeading
        }

        Text {
            Layout.fillWidth: true
            text: "Keep engine and interface preferences ready to move between setups."
            color: Theme.textSecondary
            font: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ProfileSettings {
            Layout.fillWidth: true
        }
    }
}
