/**
 * @file main.qml
 * @brief Root QML file for ChadVis modern GUI
 *
 * Main application window with:
 * - ProjectM visualizer canvas (central)
 * - Accordion sidebar (collapsible panels)
 * - Top bar (window controls)
 * - Status bar (playback info)
 *
 * @version 1.0.0
 */

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import "styles"
import "components"
import "panels"

Window {
    id: mainWindow

    visible: true
    width: 1400
    height: 900
    minimumWidth: 1024
    minimumHeight: 768

    title: {
        var title = "ChadVis"
        if (AudioBridge.currentTrack.title) {
            title = AudioBridge.currentTrack.artist + " - " + AudioBridge.currentTrack.title + " | " + title
        }
        if (RecordingBridge.isRecording) {
            title = "⏺ " + title
        }
        return title
    }

    color: Theme.background

    // ═══════════════════════════════════════════════════════════
    // MAIN LAYOUT
    // ═══════════════════════════════════════════════════════════

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ─────────────────────────────────────────────────────────
        // ACCORDION SIDEBAR
        // ─────────────────────────────────────────────────────────

        Rectangle {
            id: sidebar

            Layout.preferredWidth: sidebarExpanded ? Theme.sidebarExpandedWidth : Theme.sidebarCollapsedWidth
            Layout.fillHeight: true

            color: Theme.surface

            property bool sidebarExpanded: true

            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Sidebar header
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.topBarHeight
                    color: Theme.backgroundAlt

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingSmall

                        Text {
                            text: "ChadVis"
                            color: Theme.accent
                            font: Theme.fontSubtitle
                            visible: sidebar.sidebarExpanded
                        }

                        Item { Layout.fillWidth: true }

                        AppButton {
                            icon: sidebar.sidebarExpanded ? "qrc:/icons/collapse.svg" : "qrc:/icons/expand.svg"
                            flat: true
                            implicitWidth: 28
                            implicitHeight: 28
                            radius: Theme.radiusSmall
                            onClicked: sidebar.sidebarExpanded = !sidebar.sidebarExpanded
                        }
                    }
                }

                // Accordion panels
                AccordionContainer {
                    id: accordionContainer

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: Theme.spacingTiny

                    expandedPanel: "playback"

                    panels: [
                        {
                            id: "playback",
                            title: "Playback",
                            icon: "qrc:/icons/play.svg",
                            expandedHeight: 280,
                            component: playbackPanelComponent
                        },
                        {
                            id: "playlist",
                            title: "Library",
                            icon: "qrc:/icons/playlist.svg",
                            expandedHeight: 300,
                            component: playlistPanelComponent
                        },
                        {
                            id: "presets",
                            title: "Presets",
                            icon: "qrc:/icons/preset.svg",
                            expandedHeight: 200,
                            component: presetsPanelComponent
                        },
                        {
                            id: "recording",
                            title: "Recording",
                            icon: "qrc:/icons/record.svg",
                            expandedHeight: 180,
                            component: recordingPanelComponent
                        }
                    ]
                }
            }

            // Component definitions for lazy loading
            Component {
                id: playbackPanelComponent
                PlaybackPanel {}
            }

            Component {
                id: playlistPanelComponent
                PlaylistPanel {}
            }

            Component {
                id: presetsPanelComponent
                ColumnLayout {
                    Text { text: "Presets Panel (TODO)"; color: Theme.textSecondary }
                    Item { Layout.fillHeight: true }
                }
            }

            Component {
                id: recordingPanelComponent
                ColumnLayout {
                    spacing: Theme.spacingSmall

                    Text {
                        text: RecordingBridge.isRecording ? "● Recording" : "Ready"
                        color: RecordingBridge.isRecording ? Theme.recording : Theme.textSecondary
                        font: Theme.fontBodyStrong
                    }

                    Text {
                        text: "Duration: " + Theme.formatTime(RecordingBridge.duration * 1000)
                        color: Theme.textSecondary
                        font: Theme.fontCaption
                    }

                    AppButton {
                        text: RecordingBridge.isRecording ? "Stop" : "Start"
                        highlighted: RecordingBridge.isRecording
                        Layout.fillWidth: true
                        onClicked: {
                            if (RecordingBridge.isRecording) {
                                RecordingBridge.stopRecording()
                            } else {
                                RecordingBridge.startRecording()
                            }
                        }
                    }
                }
            }
        }

        // ─────────────────────────────────────────────────────────
        // VISUALIZER AREA (placeholder - requires QQuickItem bridge)
        // ─────────────────────────────────────────────────────────

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true

            color: Theme.background

            // Placeholder for ProjectM visualizer
            // Will be replaced with VisualizerItem QQuickItem
            Text {
                anchors.centerIn: parent
                text: "ProjectM Visualizer\n(QML Integration Pending)"
                color: Theme.textSecondary
                font: Theme.fontHeading
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusMedium
                visible: !RecordingBridge.isRecording
            }

            // Recording indicator
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spacingMedium
                width: recordingIndicatorRow.implicitWidth + Theme.spacingMedium
                height: 32
                radius: Theme.radiusMedium
                color: Theme.recording
                visible: RecordingBridge.isRecording

                RowLayout {
                    id: recordingIndicatorRow
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall

                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        color: Theme.textPrimary

                        SequentialAnimation on opacity {
                            running: RecordingBridge.isRecording
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 500 }
                            NumberAnimation { to: 1.0; duration: 500 }
                        }
                    }

                    Text {
                        text: "REC"
                        color: Theme.textPrimary
                        font: Theme.fontCaption
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // STATUS BAR
    // ═══════════════════════════════════════════════════════════

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.statusBarHeight

        color: Theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingSmall
            anchors.rightMargin: Theme.spacingSmall

            Text {
                text: AudioBridge.isPlaying ? "▶ Playing" : "⏹ Stopped"
                color: Theme.textSecondary
                font: Theme.fontCaption
            }

            Item { Layout.fillWidth: true }

            Text {
                text: "v1.0.0 • I use Arch btw"
                color: Theme.textSecondary
                font: Theme.fontCaption
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // KEYBOARD SHORTCUTS
    // ═══════════════════════════════════════════════════════════

    Shortcut {
        sequence: "Space"
        onActivated: AudioBridge.togglePlayPause()
    }

    Shortcut {
        sequence: "Right"
        onActivated: AudioBridge.next()
    }

    Shortcut {
        sequence: "Left"
        onActivated: AudioBridge.previous()
    }

    Shortcut {
        sequence: "F"
        onActivated: console.log("Fullscreen toggle (TODO)")
    }

    Shortcut {
        sequence: "R"
        onActivated: {
            if (RecordingBridge.isRecording) {
                RecordingBridge.stopRecording()
            } else {
                RecordingBridge.startRecording()
            }
        }
    }
}
