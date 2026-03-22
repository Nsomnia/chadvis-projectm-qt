/**
 * @file PlaylistPanel.qml
 * @brief Playlist display for accordion panel
 *
 * Single-responsibility: Playlist list view in accordion context
 *
 * @version 1.0.0
 */

import QtQuick
import Qt5Compat.GraphicalEffects
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

ColumnLayout {
    id: root

    spacing: Theme.spacingSmall

    // ═══════════════════════════════════════════════════════════
    // TOOLBAR
    // ═══════════════════════════════════════════════════════════

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTiny

        AppButton {
            text: "Add"
            icon: "qrc:/icons/plus.svg"
            flat: true
            implicitHeight: 28
            radius: Theme.radiusSmall
            onClicked: fileDialog.open()
        }

        AppButton {
            icon: "qrc:/icons/shuffle.svg"
            flat: true
            implicitWidth: 28
            implicitHeight: 28
            radius: Theme.radiusSmall
            onClicked: PlaylistBridge.shuffle()
        }

        AppButton {
            icon: "qrc:/icons/clear.svg"
            flat: true
            implicitWidth: 28
            implicitHeight: 28
            radius: Theme.radiusSmall
            onClicked: PlaylistBridge.clear()
        }

        Item { Layout.fillWidth: true }

        Text {
            text: PlaylistBridge.count + " tracks"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }
    }

    // ═══════════════════════════════════════════════════════════
    // PLAYLIST LIST
    // ═══════════════════════════════════════════════════════════

    ListView {
        id: playlistView

        Layout.fillWidth: true
        Layout.fillHeight: true

        model: PlaylistBridge
        clip: true

       ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        delegate: Rectangle {
            id: delegateRoot

            width: playlistView.width
            height: 48
            color: isCurrent ? Theme.accent : (mouseArea.containsMouse ? Theme.surfaceOverlay : "transparent")
            radius: Theme.radiusSmall

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSmall
                anchors.rightMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall

                Text {
                    text: (index + 1) + "."
                    color: isCurrent ? Theme.textOnAccent : Theme.textSecondary
                    font: Theme.fontCaption
                    Layout.preferredWidth: 24
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Text {
                        text: title || "Unknown"
                        color: isCurrent ? Theme.textOnAccent : Theme.textPrimary
                        font: Theme.fontBody
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        text: artist || ""
                        color: isCurrent ? Theme.textOnAccent : Theme.textSecondary
                        font: Theme.fontCaption
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                Text {
                    text: durationFormatted || "0:00"
                    color: isCurrent ? Theme.textOnAccent : Theme.textSecondary
                    font: Theme.fontCaption
                }
            }

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor

                onDoubleClicked: PlaylistBridge.playAt(index)
            }

RectangularGlow {
            visible: isCurrent
            glowRadius: 2
            spread: 0.1
            color: Theme.accent
            cornerRadius: Theme.radiusSmall
            anchors.fill: parent
        }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // FILE DIALOG (placeholder - requires Qt.labs.platform)
    // ═══════════════════════════════════════════════════════════

    QtObject {
        id: fileDialog
        function open() {
            console.log("FileDialog: Add files (requires native dialog integration)")
        }
    }
}
