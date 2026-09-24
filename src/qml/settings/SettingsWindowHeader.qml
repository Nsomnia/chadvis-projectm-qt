import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../components"

ToolBar {
    id: root

    property string eyebrow: ""
    property string title: ""

    signal closeRequested()

    implicitHeight: 64

    background: Rectangle {
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingMedium
        spacing: Theme.spacingMedium

        Rectangle {
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: Theme.radiusMedium
            color: Theme.glassHighlight
            border.width: 1
            border.color: Theme.glassBorder

            Text {
                anchors.centerIn: parent
                text: "S"
                color: Theme.accent
                font: Theme.fontSubtitle
            }
        }

        ColumnLayout {
            spacing: 0

            Text {
                text: root.eyebrow.toUpperCase()
                color: Theme.accent
                font: Theme.fontTiny
            }

            Text {
                text: root.title
                color: Theme.textPrimary
                font: Theme.fontTitle
            }
        }

        Item { Layout.fillWidth: true }

        AppButton {
            icon: "qrc:/qt/qml/ChadVis/resources/icons/clear.svg"
            flat: true
            implicitWidth: 36
            implicitHeight: 36
            buttonRadius: Theme.radiusMedium
            onClicked: root.closeRequested()
            ToolTip.visible: hovered
            ToolTip.text: "Close settings"
            ToolTip.delay: 350
            Accessible.name: "Close settings"
        }
    }
}
