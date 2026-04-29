import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis

ColumnLayout {
    id: root
    spacing: Theme.spacingMedium

    property bool showGlow: true
    property real verticalPos: 0.8

    Label {
        text: "Karaoke Aesthetics"
        font.bold: true
        color: Theme.accent
    }

    RowLayout {
        Label { text: "Glow Effects"; Layout.fillWidth: true }
        Switch {
            checked: root.showGlow
            onToggled: root.showGlow = checked
        }
    }

    ColumnLayout {
        spacing: 2
        Label { text: "Vertical Alignment" }
        Slider {
            Layout.fillWidth: true
            from: 0.1
            to: 0.9
            value: root.verticalPos
            onValueChanged: root.verticalPos = value
        }
    }

    RowLayout {
        Label { text: "Accent Color"; Layout.fillWidth: true }
        Rectangle {
            width: 24; height: 24; radius: 4
            color: Theme.accent
            border.color: Theme.textPrimary
            MouseArea {
                anchors.fill: parent
                onClicked: console.log("Color picker TODO")
            }
        }
    }
}
