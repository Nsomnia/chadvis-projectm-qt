/**
 * @file main.qml
 * @brief Suno-first desktop shell with a persistent projectM Video surface
 *
 * Navigation is Library → Create → Listen → Video → Settings. Settings is
 * a second ApplicationWindow owned by this file and opened on the same
 * QQmlApplicationEngine. The Video view remains instantiated for the whole
 * process lifetime so its native projectM QWindow and GL context are never
 * recreated while switching pages.
 *
 * Persistence continues to use the existing SettingsBridge UI keys:
 *   - expandedPanel → active content view
 *   - sidebarWidth  → expanded or collapsed navigation rail
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
import ChadVis
import "components"
import "views"

ApplicationWindow {
    id: mainWindow

    visible: true
    width: 1400
    height: 900
    minimumWidth: 800
    minimumHeight: 600

    readonly property var viewMeta: {
        "library":  { label: "Library" },
        "create":   { label: "Create" },
        "listen":   { label: "Listen" },
        "video":    { label: "Video" },
        "settings": { label: "Settings" }
    }

    property string activeView: "library"
    property string returnView: "library"
    readonly property var settingsWindowApi: settingsWindow

    onActiveViewChanged: {
        if (activeView !== "settings")
            SettingsBridge.expandedPanel = activeView
    }

    function navigate(viewId) {
        if (!viewMeta.hasOwnProperty(viewId))
            return

        if (viewId === "settings") {
            if (activeView !== "settings")
                returnView = activeView
            activeView = "settings"
            settingsWindowApi["open"]()
            settingsWindow.raise()
            settingsWindow.requestActivate()
            return
        }

        activeView = viewId
        if (settingsWindow.visible)
            settingsWindow.close()
    }

    property bool railUserExpanded: true
    readonly property bool railEffectiveExpanded:
        railUserExpanded && width >= Theme.navRailAutoCollapseBelow

    function setRailExpanded(expanded) {
        railUserExpanded = expanded
        SettingsBridge.sidebarWidth = expanded ? Theme.navRailWidthExpanded
                                               : Theme.navRailWidthCollapsed
    }

    Component.onCompleted: {
        const savedView = String(SettingsBridge.expandedPanel)
        const contentViews = ["library", "create", "listen", "video"]
        activeView = contentViews.indexOf(savedView) >= 0 ? savedView : "library"
        returnView = activeView
        railUserExpanded = SettingsBridge.sidebarWidth > 100
    }

    onClosing: SettingsBridge.save()

    title: {
        var result = "ChadVis"
        if (AudioBridge.currentTrack.title)
            result = AudioBridge.currentTrack.artist + " — " + AudioBridge.currentTrack.title + " | " + result
        if (RecordingBridge.isRecording)
            result = "REC — " + result
        return result
    }

    background: Rectangle {
        color: Theme.background
    }

    palette.window: Theme.background
    palette.windowText: Theme.textPrimary
    palette.base: Theme.surfaceRaised
    palette.alternateBase: Theme.backgroundAlt
    palette.text: Theme.textPrimary
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.textPrimary
    palette.brightText: Theme.textPrimary
    palette.light: Theme.surfaceOverlay
    palette.midlight: Theme.borderLight
    palette.mid: Theme.border
    palette.dark: Theme.backgroundAlt
    palette.shadow: Theme.withAlpha(Theme.background, 0.65)
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.textOnAccent
    palette.link: Theme.textLink
    palette.linkVisited: Theme.accentLight
    palette.toolTipBase: Theme.surfaceOverlay
    palette.toolTipText: Theme.textPrimary

    header: ToolBar {
        implicitHeight: Theme.topBarHeight

        background: Rectangle {
            color: Theme.surface

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingSmall
            spacing: Theme.spacingMedium

            Text {
                text: mainWindow.viewMeta[mainWindow.activeView].label
                color: Theme.accent
                font: Theme.fontSubtitle
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.topBarHeight - 16
                color: Theme.border
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                PulseIndicator {
                    Layout.preferredWidth: Theme.iconSmall
                    Layout.preferredHeight: Theme.iconSmall
                    active: AudioBridge.isPlaying
                    baseColor: Theme.success
                    size: Theme.iconSmall
                    dimOpacity: 0.5
                    periodMs: 800
                }

                Text {
                    Layout.fillWidth: true
                    text: AudioBridge.currentTrack.title || "No Track Selected"
                    color: Theme.textPrimary
                    font: Theme.fontBody
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Text {
                    visible: AudioBridge.currentTrack.artist !== ""
                    text: AudioBridge.currentTrack.artist
                        ? "— " + AudioBridge.currentTrack.artist
                        : ""
                    color: Theme.textSecondary
                    font: Theme.fontBody
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
            }

            Rectangle {
                visible: RecordingBridge.isRecording
                implicitWidth: recRow.implicitWidth + Theme.spacingMedium
                implicitHeight: 24
                radius: Theme.radiusSmall
                color: Theme.recording

                RowLayout {
                    id: recRow
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall

                    PulseIndicator {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        active: true
                        baseColor: Theme.textPrimary
                    }

                    Text {
                        text: "REC"
                        color: Theme.textPrimary
                        font: Theme.fontCaptionStrong
                    }
                }
            }

            AccountChip {
                onNavigateRequested: mainWindow.navigate("settings")
            }
        }
    }

    footer: ToolBar {
        implicitHeight: Theme.statusBarHeight

        background: Rectangle {
            color: Theme.surface

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.border
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingSmall
            anchors.rightMargin: Theme.spacingSmall

            Text {
                text: AudioBridge.isPlaying ? "Playing" : "Stopped"
                color: Theme.textSecondary
                font: Theme.fontCaption
            }

            Item { Layout.fillWidth: true }

            Text {
                text: PresetBridge.currentPreset && PresetBridge.currentPreset.name
                      ? PresetBridge.currentPreset.name
                      : "No Preset"
                color: Theme.textSecondary
                font: Theme.fontCaption
                elide: Text.ElideRight
                Layout.maximumWidth: 200
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.statusBarHeight - 8
                color: Theme.border
            }

            Text {
                text: "v2.0.0 · Suno Desktop"
                color: Theme.textSecondary
                font: Theme.fontCaption
            }
        }
    }

    Item {
        anchors.fill: parent

        NavRail {
            id: navRail
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            z: 10

            activeView: mainWindow.activeView
            expanded: mainWindow.railEffectiveExpanded

            onNavigate: function(viewId) {
                mainWindow.navigate(viewId)
            }
            onExpandToggled: mainWindow.setRailExpanded(!mainWindow.railUserExpanded)
        }

        Item {
            id: viewHost
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.left: navRail.right
            clip: true

            LibraryView {
                anchors.fill: parent
                visible: mainWindow.activeView === "library"
                opacity: visible ? 1 : 0
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.durationNormal
                        easing.type: Easing.OutCubic
                    }
                }
            }

            CreateView {
                anchors.fill: parent
                visible: mainWindow.activeView === "create"
                opacity: visible ? 1 : 0
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.durationNormal
                        easing.type: Easing.OutCubic
                    }
                }
            }

            ListenView {
                anchors.fill: parent
                visible: mainWindow.activeView === "listen"
                opacity: visible ? 1 : 0
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.durationNormal
                        easing.type: Easing.OutCubic
                    }
                }
            }

            // Persistent for the process lifetime: WindowContainer owns the
            // native projectM QWindow, so this view must never be unloaded.
            VideoView {
                anchors.fill: parent
                visible: mainWindow.activeView === "video"
                opacity: visible ? 1 : 0
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.durationNormal
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }

    SettingsWindow {
        id: settingsWindow
        transientParent: mainWindow
        modality: Qt.ApplicationModal
        onClosing: {
            if (mainWindow.activeView === "settings") {
                Qt.callLater(function() {
                    mainWindow.activeView = mainWindow.returnView
                })
            }
        }
    }

    Shortcut {
        sequence: SettingsBridge.keyboardPlayPause
        enabled: !settingsWindow.visible
        onActivated: AudioBridge.togglePlayPause()
    }

    Shortcut {
        sequence: SettingsBridge.keyboardNextTrack
        enabled: !settingsWindow.visible
        onActivated: AudioBridge.next()
    }

    Shortcut {
        sequence: SettingsBridge.keyboardPrevTrack
        enabled: !settingsWindow.visible
        onActivated: AudioBridge.previous()
    }

    // Fullscreen remains a QML placeholder until VisualizerBridge exposes
    // a toggle; the action now reveals the surface it belongs to.
    Shortcut {
        sequence: SettingsBridge.keyboardToggleFullscreen
        enabled: !settingsWindow.visible
        onActivated: {
            mainWindow.navigate("video")
            console.log("Fullscreen toggle (TODO)")
        }
    }

    Shortcut {
        sequence: SettingsBridge.keyboardToggleRecord
        enabled: !settingsWindow.visible
        onActivated: {
            mainWindow.navigate("video")
            if (RecordingBridge.isRecording)
                RecordingBridge.stopRecording()
            else
                RecordingBridge.startRecording()
        }
    }

    Shortcut {
        sequence: SettingsBridge.keyboardNextPreset
        enabled: !settingsWindow.visible
        onActivated: {
            mainWindow.navigate("video")
            VisualizerBridge.nextPreset()
        }
    }

    Shortcut {
        sequence: SettingsBridge.keyboardPrevPreset
        enabled: !settingsWindow.visible
        onActivated: {
            mainWindow.navigate("video")
            VisualizerBridge.previousPreset()
        }
    }

    Shortcut {
        sequence: "M"
        enabled: !settingsWindow.visible
        onActivated: mainWindow.setRailExpanded(!mainWindow.railUserExpanded)
    }
}
