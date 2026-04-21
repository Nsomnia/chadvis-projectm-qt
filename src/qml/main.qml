/**
* @file main.qml
* @brief Root QML file for ChadVis modern GUI
*
* Main application window with:
* - ProjectM visualizer canvas (central - full screen by default)
* - Drawer-based sidebar (hamburger menu style)
* - Top ToolBar header with controls
* - Status bar footer (playback info)
*
* @version 2.0.0 - Pseudo-Mobile Desktop Layout
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
    // HEADER TOOLBAR
    // ═══════════════════════════════════════════════════════════

    header: ToolBar {
        id: headerToolBar

        implicitHeight: Theme.topBarHeight
        background: Rectangle {
            color: Theme.surface
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.border
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingSmall
            anchors.rightMargin: Theme.spacingSmall
            spacing: Theme.spacingSmall

            // Hamburger menu button
            AppButton {
                icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
                flat: true
                implicitWidth: Theme.iconLarge + Theme.spacingSmall
                implicitHeight: Theme.iconLarge
                radius: Theme.radiusSmall
                onClicked: sidebarDrawer.open()
                ToolTip.visible: hovered
                ToolTip.text: "Open menu"
                ToolTip.delay: 500
            }

            // Current track info
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.topBarHeight - Theme.spacingSmall
                color: "transparent"
                clip: true

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    spacing: Theme.spacingSmall

                    // Play/Pause indicator
                    Rectangle {
                        Layout.preferredWidth: Theme.iconSmall
                        Layout.preferredHeight: Theme.iconSmall
                        radius: width / 2
                        color: AudioBridge.isPlaying ? Theme.success : Theme.textDisabled

                        SequentialAnimation on opacity {
                            running: AudioBridge.isPlaying
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.5; duration: 800 }
                            NumberAnimation { to: 1.0; duration: 800 }
                        }
                    }

                    // Track title
                    Text {
                        Layout.fillWidth: true
                        text: AudioBridge.currentTrack.title || "No Track Selected"
                        color: Theme.textPrimary
                        font: Theme.fontSubtitle
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    // Artist
                    Text {
                        visible: AudioBridge.currentTrack.artist !== ""
                        text: AudioBridge.currentTrack.artist ? "• " + AudioBridge.currentTrack.artist : ""
                        color: Theme.textSecondary
                        font: Theme.fontBody
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
            }

            // Recording indicator (in header when active)
            Rectangle {
                visible: RecordingBridge.isRecording
                implicitWidth: recordingHeaderRow.implicitWidth + Theme.spacingMedium
                implicitHeight: 28
                radius: Theme.radiusSmall
                color: Theme.recording

                RowLayout {
                    id: recordingHeaderRow
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall

                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
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
                        font.weight: Font.Bold
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // FOOTER STATUS BAR
    // ═══════════════════════════════════════════════════════════

    footer: ToolBar {
        id: footerStatusBar

        implicitHeight: Theme.statusBarHeight
        background: Rectangle {
            color: Theme.surface
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.border
            }
        }

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
                text: (PresetBridge.currentPreset && PresetBridge.currentPreset.name) ? PresetBridge.currentPreset.name : "No Preset"
                color: Theme.textSecondary
                font: Theme.fontCaption
                elide: Text.ElideRight
                Layout.maximumWidth: 200
            }

            Rectangle {
                width: 1
                height: Theme.statusBarHeight - 8
                color: Theme.border
            }

            Text {
                text: "v1.0.0 • I use Arch btw"
                color: Theme.textSecondary
                font: Theme.fontCaption
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // CENTRAL VISUALIZER AREA (MAXIMUM REAL ESTATE)
    // ═══════════════════════════════════════════════════════════

    Item {
        anchors.fill: parent

        WindowContainer {
            id: visualizerContainer
            anchors.fill: parent
            window: VisualizerBridge.visualizerWindow
            visible: VisualizerBridge.visualizerWindow !== null
        }

        VisualizerOverlay {
            id: visualizerOverlay
            anchors.fill: parent
        }

        Column {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Theme.spacingLarge + Theme.statusBarHeight
            spacing: Theme.spacingSmall

            Text {
                text: "ProjectM v4 • " + (AudioBridge.isPlaying ? "Playing" : "Ready")
                color: Theme.onBackground
                font: Theme.fontCaption
                opacity: 0.5
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: Theme.border
            border.width: 1
            visible: !RecordingBridge.isRecording
        }
    }

    // ═══════════════════════════════════════════════════════════
    // DRAWER SIDEBAR (HAMBURGER MENU STYLE)
    // ═══════════════════════════════════════════════════════════

    Drawer {
        id: sidebarDrawer

        edge: Qt.LeftEdge
        width: Math.min(Theme.sidebarExpandedWidth, mainWindow.width * 0.85)
        height: mainWindow.height

        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.border
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

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
                    }

                    Item { Layout.fillWidth: true }

                    AppButton {
                        icon: "qrc:/qt/qml/ChadVis/resources/icons/clear.svg"
                        flat: true
                        implicitWidth: 28
                        implicitHeight: 28
                        radius: Theme.radiusSmall
                        onClicked: sidebarDrawer.close()
                    }
                }
            }

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
                    },
                    {
                        id: "settings",
                        title: "Settings",
                        icon: "qrc:/qt/qml/ChadVis/resources/icons/qml/playback.svg",
                        expandedHeight: 300,
                        component: settingsPanelComponent
                    }
                ]
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // COMPONENT DEFINITIONS FOR LAZY LOADING
    // ═══════════════════════════════════════════════════════════

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

    Component {
        id: settingsPanelComponent
        SettingsPanel {}
    }

    // ═══════════════════════════════════════════════════════════
    // KEYBOARD SHORTCUTS (PRESERVED)
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

    Shortcut {
        sequence: "M"
        onActivated: {
            if (sidebarDrawer.visible) {
                sidebarDrawer.close()
            } else {
                sidebarDrawer.open()
            }
        }
    }
}
