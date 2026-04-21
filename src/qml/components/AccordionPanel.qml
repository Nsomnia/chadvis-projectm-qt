import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis

ColumnLayout {
    id: root
    property string title: "Panel"
    property string iconSource: ""
    property bool expanded: false
    property alias content: contentLoader.sourceComponent

    Layout.fillWidth: true
    spacing: 0

    // Header
    Rectangle {
        id: header
        Layout.fillWidth: true
        height: 48
        color: expanded ? Theme.surfaceRaised : Theme.surface
        
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Image {
                width: 24; height: 24
                source: iconSource
                sourceSize: Qt.size(24, 24)
                fillMode: Image.PreserveAspectFit
                visible: iconSource !== ""
            }

            Text {
                text: title
                color: expanded ? Theme.accent : Theme.textPrimary
                font: Theme.fontBodyBold
                Layout.fillWidth: true
            }

            Image {
                width: 16; height: 16
                source: "qrc:/ChadVis/resources/icons/expand.svg"
                rotation: expanded ? 180 : 0
                sourceSize: Qt.size(16, 16)
                Behavior on rotation { NumberAnimation { duration: 200 } }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: expanded = !expanded
        }
    }

    // Content
    Rectangle {
        id: contentContainer
        Layout.fillWidth: true
        Layout.preferredHeight: expanded ? -1 : 0
        clip: true
        color: Theme.surface
        
        Behavior on Layout.preferredHeight {
            NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
        }

        Loader {
            id: contentLoader
            anchors.fill: parent
            anchors.margins: expanded ? Theme.spacingSmall : 0
            opacity: expanded ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
    }
    
    // Bottom Border
    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: Theme.border
    }
}
