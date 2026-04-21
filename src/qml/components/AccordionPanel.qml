/**
 * @file AccordionPanel.qml
 * @brief Collapsible panel with header and animated content area
 */

import QtQuick
import Qt5Compat.GraphicalEffects
import QtQuick.Layouts
import ChadVis

Item {
    id: root

    property string panelId: ""
    property string title: "Panel"
    property string icon: ""
    property bool isExpanded: false
    property int collapsedHeight: Theme.panelHeaderHeight
    property int expandedHeight: 250
    property alias contentComponent: contentLoader.sourceComponent

    signal headerClicked()

    implicitHeight: isExpanded ? expandedHeight : collapsedHeight
    clip: true

    Behavior on implicitHeight {
        NumberAnimation {
            duration: Theme.durationNormal
            easing.type: Easing.OutCubic
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 1

        Rectangle {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: root.collapsedHeight
            color: root.isExpanded ? Theme.accent : Theme.surface
            radius: Theme.radiusMedium

            gradient: Gradient {
                GradientStop { position: 0; color: root.isExpanded ? Theme.accent : Qt.lighter(Theme.surface, 1.1) }
                GradientStop { position: 1; color: root.isExpanded ? Qt.darker(Theme.accent, 1.1) : Theme.surface }
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.glassBackground
                opacity: root.isExpanded ? 0.2 : 0.85
                radius: Theme.radiusMedium
                border.color: root.isExpanded ? Theme.accentLight : Theme.border
                border.width: 1
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSmall
                anchors.rightMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall

                Image {
                    visible: root.icon !== ""
                    source: root.icon
                    sourceSize.width: Theme.iconMedium
                    sourceSize.height: Theme.iconMedium
                    Layout.preferredWidth: Theme.iconMedium
                    Layout.preferredHeight: Theme.iconMedium
                    fillMode: Image.PreserveAspectFit

                    ColorOverlay {
                        anchors.fill: parent
                        source: parent
                        color: root.isExpanded ? Theme.textOnAccent : Theme.textSecondary
                    }
                }

                Text {
                    text: root.title
                    color: root.isExpanded ? Theme.textOnAccent : Theme.textPrimary
                    font: Theme.fontBodyStrong
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Text {
                    text: "▶"
                    color: root.isExpanded ? Theme.textOnAccent : Theme.textSecondary
                    font.pixelSize: 10
                    rotation: root.isExpanded ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true
                onClicked: root.headerClicked()
            }
        }

        Rectangle {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 1
            visible: root.isExpanded
            color: Theme.surface
            radius: Theme.radiusMedium

            Loader {
                id: contentLoader
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                active: root.isExpanded
                opacity: root.isExpanded ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            }
        }
    }
}
