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

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
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
                expandedHeight: 350,
                component: presetsPanelComponent
            },
            {
                id: "lyrics",
                title: "Lyrics",
                icon: "qrc:/icons/lyrics.svg",
                expandedHeight: 300,
                component: lyricsPanelComponent
            },
            {
                id: "suno",
                title: "Suno",
                icon: "qrc:/icons/suno.svg",
                expandedHeight: 400,
                component: sunoPanelComponent
            },
            {
                id: "overlay",
                title: "Overlay",
                icon: "qrc:/icons/overlay.svg",
                expandedHeight: 300,
                component: overlayPanelComponent
            },
            {
                id: "recording",
                title: "Recording",
                icon: "qrc:/icons/record.svg",
                expandedHeight: 350,
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
        PresetsPanel {}
    }

    Component {
        id: lyricsPanelComponent
        LyricsPanel {}
    }

    Component {
        id: sunoPanelComponent
        SunoPanel {}
    }

    Component {
        id: overlayPanelComponent
        OverlayPanel {}
    }

    Component {
        id: recordingPanelComponent
        RecordingPanel {}
    }
}

// ─────────────────────────────────────────────────────────
// VISUALIZER AREA
// ─────────────────────────────────────────────────────────

Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true

        color: Theme.background

        // Audio-reactive visualizer placeholder
        // Uses gradient animation to simulate visualizer
        Rectangle {
            id: visualizerBackground
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: Qt.darker(Theme.background, 1.2) }
                GradientStop { position: 0.5; color: Theme.background }
                GradientStop { position: 1.0; color: Qt.darker(Theme.background, 1.3) }
            }

            // Animated bars to simulate audio visualization
            Row {
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: 32
                    Rectangle {
                        width: 8
                        height: Math.max(4, (Math.sin(index * 0.3 + Date.now() * 0.002) * 0.5 + 0.5) * parent.height * 0.6)
                        color: Theme.accent
                        opacity: 0.7
                        radius: 2

                        Behavior on height {
                            NumberAnimation { duration: 50 }
                        }

                        // Continuous animation
                        Timer {
                            interval: 50
                            running: AudioBridge.isPlaying
                            repeat: true
                            onTriggered: parent.height = Math.max(4, (Math.sin(index * 0.3 + Date.now() * 0.002) * 0.5 + 0.5) * visualizerBackground.height * 0.6)
                        }
                    }
                }
            }

            // Visualizer info overlay
            Column {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: Theme.spacingLarge
                spacing: Theme.spacingSmall

                Text {
                    text: "ProjectM v4 Ready"
                    color: Theme.onBackground
                    font: Theme.fontCaption
                    opacity: 0.5
                }
            }
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
