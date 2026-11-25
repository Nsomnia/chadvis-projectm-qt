/**
 * @file LineageNode.qml
 * @brief One song in the lineage constellation.
 *
 * Presentation only: it is told what to show and reports a tap. The focus clip
 * reads differently from its relatives, and an inferred link is visibly a guess
 * (dashed border, "guess" badge) next to a server-attributed one — the whole point
 * of the view is that the user can tell which is which without reading a manual.
 */

import QtQuick
import QtQuick.Layouts
import ChadVis

Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property string badge: ""
    property real confidence: 0
    property bool inferred: true
    property bool current: false

    signal activated()

    implicitWidth: 208
    implicitHeight: 76
    radius: Theme.radiusMedium
    color: root.current ? Theme.glassHighlight : Theme.surfaceRaised
    border.width: root.current ? 2 : 1
    border.color: root.current ? Theme.accent
                              : (nodeMouse.containsMouse ? Theme.borderFocus
                                                         : (root.inferred ? Theme.border
                                                                          : Theme.accentDark))

    Accessible.role: Accessible.ListItem
    Accessible.name: root.title
    Accessible.description: root.badge + " · " + Math.round(root.confidence * 100) + "%"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingSmall
        spacing: 2

        Text {
            Layout.fillWidth: true
            text: root.title
            color: root.current ? Theme.accent : Theme.textPrimary
            font: root.current ? Theme.fontBodyStrong : Theme.fontBody
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTiny

            Rectangle {
                visible: root.badge !== ""
                implicitWidth: badgeText.implicitWidth + Theme.spacingSmall
                implicitHeight: badgeText.implicitHeight + 4
                radius: Theme.radiusSmall
                color: Theme.withAlpha(root.inferred ? Theme.textSecondary : Theme.accent, 0.18)

                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: root.badge
                    color: root.inferred ? Theme.textSecondary : Theme.accent
                    font: Theme.fontTiny
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                visible: root.confidence > 0
                text: Math.round(root.confidence * 100) + "%"
                color: Theme.textDisabled
                font: Theme.fontTiny
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.subtitle !== ""
            text: root.subtitle
            color: Theme.textSecondary
            font: Theme.fontTiny
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    MouseArea {
        id: nodeMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }
}
