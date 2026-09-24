pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import ChadVis

Rectangle {
    id: root

    property var pages: []
    property int currentPage: 0

    signal navigate(int pageIndex)

    Layout.preferredWidth: 220
    Layout.fillHeight: true
    color: Theme.backgroundAlt

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingTiny

        Text {
            Layout.leftMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingSmall
            text: "SETTINGS"
            color: Theme.textDisabled
            font: Theme.fontTiny
        }

        Repeater {
            model: root.pages

            delegate: PageRailItem {
                required property var modelData
                required property int index

                label: modelData.title
                pageIndex: index
                selected: root.currentPage === index

                onActivated: root.navigate(index)
            }
        }

        Item { Layout.fillHeight: true }
    }

    component PageRailItem: Rectangle {
        id: item

        property string label: ""
        property int pageIndex: -1
        property bool selected: false
        readonly property bool hovered: railMouse.containsMouse || activeFocus

        signal activated()

        Layout.fillWidth: true
        Layout.preferredHeight: 40
        activeFocusOnTab: true
        radius: Theme.radiusMedium
        color: selected ? Theme.glassHighlight
                        : hovered ? Theme.surfaceRaised
                        : Theme.withAlpha(Theme.backgroundAlt, 0)
        border.width: activeFocus || selected ? 1 : 0
        border.color: activeFocus ? Theme.borderFocus : Theme.glassBorder

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: item.selected ? 22 : 0
            radius: 1.5
            color: Theme.accent

            Behavior on height {
                NumberAnimation {
                    duration: Theme.durationNormal
                    easing.type: Easing.OutCubic
                }
            }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMedium
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMedium
            anchors.verticalCenter: parent.verticalCenter
            text: item.label
            color: item.selected ? Theme.accent
                                : item.hovered ? Theme.textPrimary
                                : Theme.textPrimaryVariant
            font: item.selected ? Theme.fontBodyStrong : Theme.fontBody
            elide: Text.ElideRight

            Behavior on color {
                ColorAnimation { duration: Theme.durationFast }
            }
        }

        MouseArea {
            id: railMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                item.forceActiveFocus()
                item.activated()
            }
        }

        Keys.onReturnPressed: item.activated()
        Keys.onSpacePressed: item.activated()

        Accessible.role: Accessible.Button
        Accessible.name: item.label
    }
}
