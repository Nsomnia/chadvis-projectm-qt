/**
 * @file PlaylistPanel.qml
 * @brief Playlist display for accordion panel
 *
 * Single-responsibility: Playlist list view in accordion context
 * Features: drag-drop, context menu, file dialog, repeat mode
 *
 * @version 1.1.0
 */

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Controls
import Qt.labs.platform as Platform
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
            icon: "qrc:/qt/qml/ChadVis/resources/icons/plus.svg"
            flat: true
            implicitHeight: 28
            radius: Theme.radiusSmall
            onClicked: fileDialog.open()
        }

        AppButton {
            icon: "qrc:/qt/qml/ChadVis/resources/icons/shuffle.svg"
            flat: true
            implicitWidth: 28
            implicitHeight: 28
            radius: Theme.radiusSmall
            onClicked: PlaylistBridge.shuffle()
        }

        AppButton {
            text: PlaylistBridge.repeatMode === 0 ? "Off" :
                  PlaylistBridge.repeatMode === 1 ? "All" : "One"
            flat: true
            implicitHeight: 28
            radius: Theme.radiusSmall
            onClicked: PlaylistBridge.cycleRepeatMode()
            ToolTip.visible: hovered
            ToolTip.text: PlaylistBridge.repeatMode === 0 ? "Repeat: Off" :
                          PlaylistBridge.repeatMode === 1 ? "Repeat: All" : "Repeat: One"
        }

        AppButton {
            icon: "qrc:/qt/qml/ChadVis/resources/icons/clear.svg"
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
    // PLAYLIST LIST WITH DROP AREA
    // ═══════════════════════════════════════════════════════════

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: "transparent"
        radius: Theme.radiusSmall

        // Drop area for file drag-drop
        DropArea {
            id: dropArea
            anchors.fill: parent

            onDropped: (drop) => {
                if (drop.hasUrls) {
                    var urls = [];
                    for (var i = 0; i < drop.urls.length; i++) {
                        urls.push(drop.urls[i]);
                    }
                    PlaylistBridge.addFiles(urls);
                    drop.acceptProposedAction();
                }
            }

            Rectangle {
                anchors.fill: parent
                color: dropArea.containsDrag ? Theme.accent : "transparent"
                opacity: dropArea.containsDrag ? 0.1 : 0
                radius: Theme.radiusSmall
                z: -1
            }
        }

        ListView {
            id: playlistView

            anchors.fill: parent
            model: PlaylistBridge
            clip: true

            // Internal drag reorder
            moveDisplaced: Transition {
                NumberAnimation { properties: "y"; duration: 200 }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                id: delegateRoot

                width: playlistView.width
                height: 48
                color: isCurrent ? Theme.accent : (mouseArea.containsMouse ? Theme.surfaceOverlay : "transparent")
                radius: Theme.radiusSmall

                // Drag handling for internal reorder
                Drag.active: dragHandler.active
                Drag.hotSpot.x: width / 2
                Drag.hotSpot.y: height / 2
                Drag.source: delegateRoot

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
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    onDoubleClicked: PlaylistBridge.playAt(index)
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            contextMenu.menuIndex = index
                            contextMenu.popup()
                        }
                    }

                    // Drag handler for internal reorder
                    DragHandler {
                        id: dragHandler
                        target: null
                        onActiveChanged: {
                            if (!active) {
                                var targetIndex = playlistView.indexAt(
                                    delegateRoot.x + dragHandler.centroid.position.x,
                                    delegateRoot.y + dragHandler.centroid.position.y
                                );
                                if (targetIndex >= 0 && targetIndex !== index) {
                                    PlaylistBridge.moveItem(index, targetIndex);
                                }
                            }
                        }
                    }
                }

        Rectangle {
            visible: isCurrent
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: Theme.accent
            opacity: 0.15
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Theme.accent
                shadowBlur: 0.5
            }
        }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // CONTEXT MENU
    // ═══════════════════════════════════════════════════════════

Menu {
    id: contextMenu

    property int menuIndex: -1

        MenuItem {
            text: "Remove"
            onTriggered: {
                if (contextMenu.menuIndex >= 0) {
                    PlaylistBridge.removeAt(contextMenu.menuIndex)
                }
            }
        }

        MenuItem {
            text: "Clear All"
            onTriggered: PlaylistBridge.clear()
        }

        MenuSeparator {}

        MenuItem {
            text: "Show in Folder"
            enabled: contextMenu.menuIndex >= 0
            onTriggered: {
                if (contextMenu.menuIndex >= 0) {
                    var path = PlaylistBridge.getItemPath(contextMenu.menuIndex)
                    if (path && path.length > 0) {
                        Qt.openUrlExternally("file://" + path)
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // FILE DIALOG (Native)
    // ═══════════════════════════════════════════════════════════

    Platform.FileDialog {
        id: fileDialog
        title: "Add Audio Files"
        fileMode: Platform.FileDialog.OpenFiles
        nameFilters: ["Audio files (*.mp3 *.flac *.wav *.ogg *.m4a *.aac *.opus)"]
        onAccepted: {
            var urls = [];
            for (var i = 0; i < selectedFiles.length; i++) {
                urls.push(selectedFiles[i]);
            }
            PlaylistBridge.addFiles(urls);
        }
    }
}
