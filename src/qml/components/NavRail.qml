pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import ChadVis

Rectangle {
    id: root

    property string activeView: "library"
    property bool expanded: true

    signal navigate(string viewId)
    signal expandToggled()

    readonly property var entries: [
        { id: "library",  label: "Library",  icon: iconUrl("playlist") },
        { id: "create",   label: "Create",   icon: iconUrl("suno") },
        { id: "listen",   label: "Listen",   icon: iconUrl("playback") },
        { id: "video",    label: "Video",    icon: iconUrl("overlay") },
        { id: "settings", label: "Settings", icon: iconUrl("presets") }
    ]

    function iconUrl(name) {
        return "qrc:/qt/qml/ChadVis/resources/icons/qml/" + name + ".svg"
    }

    width: expanded ? Theme.navRailWidthExpanded : Theme.navRailWidthCollapsed
    color: Theme.surface

    Behavior on width {
        NumberAnimation {
            duration: Theme.durationNormal
            easing.type: Easing.InOutCubic
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.topBarHeight + Theme.spacingSmall

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: Theme.radiusMedium
                    color: Theme.glassHighlight
                    border.color: Theme.glassBorder
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "CV"
                        color: Theme.accent
                        font: Theme.fontCaptionStrong
                    }
                }

                Text {
                    visible: root.expanded
                    text: "ChadVis"
                    color: Theme.accent
                    font: Theme.fontSubtitle
                    opacity: root.expanded ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMedium
            spacing: Theme.spacingTiny

            Repeater {
                model: root.entries

                delegate: Item {
                    id: navEntry

                    required property var modelData

                    readonly property bool isActive: root.activeView === navEntry.modelData.id
                    readonly property bool isHovered: entryMouse.containsMouse || activeFocus

                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingSmall
                    Layout.rightMargin: Theme.spacingSmall
                    Layout.preferredHeight: 44
                    activeFocusOnTab: true

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusMedium
                        color: navEntry.isActive ? Theme.glassHighlight
                             : navEntry.isHovered ? Theme.glassBackground
                             : Theme.withAlpha(Theme.surface, 0)
                        border.width: navEntry.activeFocus || navEntry.isActive ? 1 : 0
                        border.color: navEntry.activeFocus ? Theme.borderFocus : Theme.glassBorder

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: -Theme.spacingSmall
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: navEntry.isActive ? parent.height - 12 : 0
                        radius: 1.5
                        color: Theme.accent

                        Behavior on height {
                            NumberAnimation {
                                duration: Theme.durationNormal
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingSmall
                        spacing: Theme.spacingSmall

                        Item {
                            Layout.preferredWidth: Theme.iconMedium
                            Layout.preferredHeight: Theme.iconMedium

                            Image {
                                anchors.fill: parent
                                source: navEntry.modelData.icon
                                sourceSize: Qt.size(Theme.iconMedium, Theme.iconMedium)
                                fillMode: Image.PreserveAspectFit
                                layer.enabled: true
                                layer.effect: MultiEffect {
                                    autoPaddingEnabled: true
                                    colorization: 1
                                    colorizationColor: navEntry.isActive ? Theme.accent
                                                         : navEntry.isHovered ? Theme.textPrimary
                                                         : Theme.textSecondary
                                }
                            }
                        }

                        Text {
                            visible: root.expanded
                            text: navEntry.modelData.label
                            color: navEntry.isActive ? Theme.accent : Theme.textPrimary
                            font: navEntry.isActive ? Theme.fontBodyStrong : Theme.fontBody
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            opacity: root.expanded ? 1 : 0

                            Behavior on opacity {
                                NumberAnimation { duration: Theme.durationFast }
                            }
                        }
                    }

                    MouseArea {
                        id: entryMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            navEntry.forceActiveFocus()
                            root.navigate(navEntry.modelData.id)
                        }
                    }

                    Keys.onReturnPressed: root.navigate(navEntry.modelData.id)
                    Keys.onSpacePressed: root.navigate(navEntry.modelData.id)

                    Accessible.role: Accessible.Button
                    Accessible.name: navEntry.modelData.label

                    ToolTip.visible: !root.expanded && navEntry.isHovered
                    ToolTip.text: navEntry.modelData.label
                    ToolTip.delay: 400
                }
            }
        }

        Item { Layout.fillHeight: true }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSmall
                anchors.rightMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall

                AppButton {
                    icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
                    flat: true
                    implicitWidth: 36
                    implicitHeight: 36
                    buttonRadius: Theme.radiusMedium
                    rotation: root.expanded ? 180 : 0
                    onClicked: root.expandToggled()
                    ToolTip.visible: hovered
                    ToolTip.text: root.expanded ? "Collapse rail" : "Expand rail"
                    ToolTip.delay: 400
                    Accessible.name: root.expanded ? "Collapse navigation rail" : "Expand navigation rail"

                    Behavior on rotation {
                        NumberAnimation {
                            duration: Theme.durationNormal
                            easing.type: Easing.InOutCubic
                        }
                    }
                }

                Text {
                    visible: root.expanded
                    text: "v2.0"
                    color: Theme.textDisabled
                    font: Theme.fontTiny
                    opacity: root.expanded ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast }
                    }
                }
            }
        }
    }
}
