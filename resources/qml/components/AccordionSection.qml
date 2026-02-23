import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    property string sectionId: ""
    property string title: ""
    property string subtitle: ""
    property bool expanded: false
    property color backgroundColor: "#1A2430"
    property color accentColor: "#30C6B5"

    signal toggleRequested(string sectionId)

    default property alias contentData: contentColumn.data

    implicitWidth: parent ? parent.width : 360
    implicitHeight: headerCard.implicitHeight + contentWrapper.height + 12

    Rectangle {
        id: headerCard
        anchors.left: parent.left
        anchors.right: parent.right
        radius: 18
        color: Qt.lighter(root.backgroundColor, 1.06)
        border.width: 1
        border.color: root.expanded ? root.accentColor : "#2A3746"
        implicitHeight: 78

        RowLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: 12
                color: root.accentColor
                opacity: 0.18

                Text {
                    anchors.centerIn: parent
                    text: root.expanded ? "OPEN" : "MENU"
                    color: "#D2F5EF"
                    font.pixelSize: 9
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: root.title
                    color: "#F2F7FF"
                    font.pixelSize: 19
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: root.subtitle
                    color: "#9CB5CD"
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    visible: subtitle.length > 0
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: 12
                color: root.expanded ? root.accentColor : "#2B3A4B"

                Text {
                    anchors.centerIn: parent
                    text: root.expanded ? "-" : "+"
                    color: root.expanded ? "#07151A" : "#D1E6FF"
                    font.pixelSize: 24
                    font.bold: true
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.toggleRequested(root.sectionId)
            cursorShape: Qt.PointingHandCursor
        }
    }

    Item {
        id: contentWrapper
        anchors.top: headerCard.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        height: root.expanded ? contentColumn.implicitHeight + 16 : 0

        Behavior on height {
            NumberAnimation {
                duration: 220
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: 16
            color: root.backgroundColor
            border.width: 1
            border.color: "#253445"

            Column {
                id: contentColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 10
            }
        }
    }
}
