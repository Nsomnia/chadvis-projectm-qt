import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../components"

ToolBar {
    id: root

    property string statusMessage: ""

    signal resetRequested()
    signal saveRequested()

    implicitHeight: 64

    background: Rectangle {
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.border
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.spacingMedium

        Text {
            Layout.fillWidth: true
            text: root.statusMessage
            visible: text.length > 0
            color: Theme.success
            font: Theme.fontCaption
        }

        AppButton {
            text: "Reset to defaults"
            flat: true
            implicitHeight: Theme.buttonHeightLarge
            buttonRadius: Theme.radiusMedium
            onClicked: root.resetRequested()
            Accessible.name: "Reset settings to defaults"
        }

        AppButton {
            text: "Save"
            highlighted: true
            implicitWidth: 112
            implicitHeight: Theme.buttonHeightLarge
            buttonRadius: Theme.radiusMedium
            onClicked: root.saveRequested()
            Accessible.name: "Save settings"
        }
    }
}
