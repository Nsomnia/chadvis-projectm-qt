/**
 * @file NavRail.qml
 * @brief Persistent left navigation rail for the app shell
 *
 * Icon+label vertical nav that animates between collapsed (icons-only)
 * and expanded (icon + label) widths. Stub entries render disabled with
 * a "SOON" badge to advertise product direction without fake features.
 *
 * Emits navigate(id) on click; active highlight driven by activeView.
 *
 * @version 1.0.0 — P2 navigation re-home (docs/PIVOT_PLAN.md)
 */

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
        { id: "library",    label: "Library",    icon: iconUrl("playlist"), enabled: true },
        { id: "listen",     label: "Listen",     icon: iconUrl("playback"), enabled: true },
        { id: "canvas",     label: "Canvas",     icon: iconUrl("overlay"),  enabled: false },
        { id: "studio",     label: "Studio",     icon: iconUrl("lyrics"),   enabled: false },
        { id: "automation", label: "Automation", icon: iconUrl("random"),   enabled: false }
    ]

    function iconUrl(name) {
        return "qrc:/qt/qml/ChadVis/resources/icons/qml/" + name + ".svg"
    }

    width: expanded ? Theme.navRailWidthExpanded : Theme.navRailWidthCollapsed
    Behavior on width {
        NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutCubic }
    }

    color: Theme.surface

    // Right hairline separates rail from content
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

        // ─────────────────────────────────────────────
        // BRAND MARK
        // ─────────────────────────────────────────────
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
                    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        // ─────────────────────────────────────────────
        // NAV ENTRIES
        // ─────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMedium
            spacing: Theme.spacingTiny

            Repeater {
                model: root.entries

                delegate: Item {
                    id: navEntry

                    readonly property bool isActive: root.activeView === modelData.id && modelData.enabled
                    readonly property bool isHovered: entryMouse.containsMouse

                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingSmall
                    Layout.rightMargin: Theme.spacingSmall
                    Layout.preferredHeight: 44
                    implicitHeight: 44

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusMedium
                        color: navEntry.isActive ? Theme.glassHighlight
                             : navEntry.isHovered ? Theme.glassBackground
                             : "transparent"
                        border.width: navEntry.isActive ? 1 : 0
                        border.color: Theme.glassBorder

                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    }

                    // Active indicator bar
                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: -Theme.spacingSmall
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: navEntry.isActive ? parent.height - 12 : 0
                        radius: 1.5
                        color: Theme.accent

                        Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
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
                                source: modelData.icon
                                sourceSize: Qt.size(Theme.iconMedium, Theme.iconMedium)
                                fillMode: Image.PreserveAspectFit
                                layer.enabled: true
                                layer.effect: MultiEffect {
                                    autoPaddingEnabled: true
                                    colorization: 1.0
                                    colorizationColor: !modelData.enabled ? Theme.textDisabled
                                                     : navEntry.isActive ? Theme.accent
                                                     : navEntry.isHovered ? Theme.textPrimary
                                                     : Theme.textSecondary
                                }
                            }
                        }

                        Text {
                            visible: root.expanded
                            text: modelData.label
                            color: !modelData.enabled ? Theme.textDisabled
                                 : navEntry.isActive ? Theme.accent
                                 : Theme.textPrimary
                            font: navEntry.isActive ? Theme.fontBodyStrong : Theme.fontBody
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            opacity: root.expanded ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                        }

                        // "SOON" badge (expanded) advertises the roadmap stubs
                        Rectangle {
                            visible: root.expanded && !modelData.enabled
                            Layout.preferredWidth: soonLabel.implicitWidth + 12
                            Layout.preferredHeight: 16
                            radius: Theme.radiusRound
                            color: "transparent"
                            border.color: Theme.warningDim
                            border.width: 1

                            Text {
                                id: soonLabel
                                anchors.centerIn: parent
                                text: "SOON"
                                color: Theme.warning
                                font: Theme.fontTiny
                            }
                        }
                    }

                    // Collapsed stub marker: single amber dot
                    Rectangle {
                        visible: !root.expanded && !modelData.enabled
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 5
                        height: 5
                        radius: 2.5
                        color: Theme.warningDim
                    }

                    MouseArea {
                        id: entryMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: modelData.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                        onClicked: if (modelData.enabled) root.navigate(modelData.id)
                    }

                    ToolTip.visible: !root.expanded && navEntry.isHovered
                    ToolTip.text: modelData.label + (!modelData.enabled ? " — coming online soon" : "")
                    ToolTip.delay: 400
                }
            }
        }

        Item { Layout.fillHeight: true }

        // ─────────────────────────────────────────────
        // RAIL FOOTER: collapse toggle
        // ─────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
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
                    id: collapseButton
                    icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
                    flat: true
                    implicitWidth: 36
                    implicitHeight: 36
                    radius: Theme.radiusMedium
                    rotation: root.expanded ? 180 : 0
                    Behavior on rotation {
                        NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutCubic }
                    }
                    onClicked: root.expandToggled()
                    ToolTip.visible: hovered
                    ToolTip.text: root.expanded ? "Collapse rail" : "Expand rail"
                    ToolTip.delay: 400
                }

                Text {
                    visible: root.expanded
                    text: "v2.0"
                    color: Theme.textDisabled
                    font: Theme.fontTiny
                    opacity: root.expanded ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                }
            }
        }
    }
}
