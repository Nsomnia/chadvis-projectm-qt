import QtQuick
import QtQuick.Layouts
import ChadVis

ColumnLayout {
    id: root

    // ═══════════════════════════════════════════════════════════
    // PROPERTIES
    // ═══════════════════════════════════════════════════════════
    property string panelId: ""
    property string title: "Panel"
    property alias icon: root.iconSource
    property string iconSource: ""
    property bool isExpanded: false
    property int expandedHeight: 250
    property Component contentComponent: null

    // ═══════════════════════════════════════════════════════════
    // SIGNALS
    // ═══════════════════════════════════════════════════════════
    signal headerClicked()

    Layout.fillWidth: true
    spacing: 0

    // ═══════════════════════════════════════════════════════════
    // HEADER
    // ═══════════════════════════════════════════════════════════
    Rectangle {
        id: header
        Layout.fillWidth: true
        height: 48
        color: root.isExpanded ? Theme.surfaceRaised : Theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Image {
                width: 24; height: 24
                source: root.iconSource
                sourceSize: Qt.size(24, 24)
                fillMode: Image.PreserveAspectFit
                visible: root.iconSource !== ""
            }

            Text {
                text: root.title
                color: root.isExpanded ? Theme.accent : Theme.textPrimary
                font: Theme.fontBodyBold
                Layout.fillWidth: true
            }

            Image {
                width: 16; height: 16
                source: "qrc:/ChadVis/resources/icons/expand.svg"
                rotation: root.isExpanded ? 180 : 0
                sourceSize: Qt.size(16, 16)
                Behavior on rotation { NumberAnimation { duration: 200; easing.type: Easing.InOutCubic } }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.headerClicked()
        }
    }

    // ═══════════════════════════════════════════════════════════
    // CONTENT
    // ═══════════════════════════════════════════════════════════
    Rectangle {
        id: contentContainer
        Layout.fillWidth: true
        Layout.preferredHeight: root.isExpanded ? root.expandedHeight : 0
        clip: true
        color: Theme.surface

        Behavior on Layout.preferredHeight {
            NumberAnimation { duration: 250; easing.type: Easing.InOutCubic }
        }

        Loader {
            id: contentLoader
            anchors.fill: parent
            anchors.margins: root.isExpanded ? Theme.spacingSmall : 0
            sourceComponent: root.isExpanded ? root.contentComponent : null
            opacity: root.isExpanded ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // BOTTOM BORDER
    // ═══════════════════════════════════════════════════════════
    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: Theme.border
    }
}
