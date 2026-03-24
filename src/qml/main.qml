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
import ChadVis
import "components"
import "panels"

ApplicationWindow {
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

    background: Rectangle { color: Theme.background }

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
                            icon: sidebar.sidebarExpanded ? "qrc:/qt/qml/ChadVis/resources/icons/collapse.svg" : "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
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
                icon: "qrc:/qt/qml/ChadVis/resources/icons/qml/playback.svg",
                expandedHeight: 280,
                component: playbackPanelComponent
            },
            {
                id: "playlist",
                title: "Library",
                icon: "qrc:/qt/qml/ChadVis/resources/icons/qml/playlist.svg",
                expandedHeight: 300,
                component: playlistPanelComponent
            },
            {
                id: "presets",
                title: "Presets",
                icon: "qrc:/qt/qml/ChadVis/resources/icons/qml/presets.svg",
                expandedHeight: 350,
                component: presetsPanelComponent
            },
            {
                id: "lyrics",
                title: "Lyrics",
                icon: "qrc:/qt/qml/ChadVis/resources/icons/qml/lyrics.svg",
                expandedHeight: 300,
                component: lyricsPanelComponent
            },
            {
                id: "suno",
                title: "Suno",
                icon: "qrc:/qt/qml/ChadVis/resources/icons/qml/suno.svg",
                expandedHeight: 400,
                component: sunoPanelComponent
            },
            {
                id: "overlay",
                title: "Overlay",
                icon: "qrc:/qt/qml/ChadVis/resources/icons/qml/overlay.svg",
                expandedHeight: 300,
                component: overlayPanelComponent
            },
            {
                id: "recording",
                title: "Recording",
                icon: "qrc:/qt/qml/ChadVis/resources/icons/qml/recording.svg",
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

Item {
Layout.fillWidth: true
Layout.fillHeight: true

// Real ProjectM visualizer (OpenGL underlay renders beneath)
VisualizerItem {
id: visualizerItem
anchors.fill: parent
fps: 60
}

        // NOTE: Placeholder removed - visualizer is always visible
        // Silent PCM data is fed when no audio is playing (see VisualizerItem::feedSilentAudio)

	// Visualizer info overlay
	Column {
	    anchors.bottom: parent.bottom
	    anchors.horizontalCenter: parent.horizontalCenter
	    anchors.bottomMargin: Theme.spacingLarge
	    spacing: Theme.spacingSmall

	    Text {
	        text: (PresetBridge.currentPreset && PresetBridge.currentPreset.name) ? PresetBridge.currentPreset.name : "No Preset Selected"
	        color: Theme.onBackground
	        font: Theme.fontCaption
	        opacity: 0.7
	        visible: AudioBridge.isPlaying
	    }

	    Text {
	        text: "ProjectM v4 • " + (AudioBridge.isPlaying ? "Playing" : "Ready")
	        color: Theme.onBackground
	        font: Theme.fontCaption
	        opacity: 0.5
	    }
	}

	// Border overlay
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
