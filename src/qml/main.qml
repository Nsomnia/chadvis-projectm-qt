/**
 * @file main.qml
 * @brief ChadVis app shell — persistent nav rail over a paged view host
 *
 * P2 navigation re-home (docs/PIVOT_PLAN.md): Suno.com frontend first,
 * projectM second.
 *
 *   NavRail │ Library (default landing) · Listen (player+visualizer) ·
 *           │ Canvas† · Studio† · Automation† (roadmap stubs) · Settings
 *
 * Persistence rides existing SettingsBridge UI-state keys (no C++ changes):
 *   - expandedPanel → active view id ("library"/"listen"/…)
 *   - sidebarWidth  → rail state (>100 = expanded, else collapsed icons-only)
 *
 * The Listen view stays instantiated for the app's whole lifetime so the
 * embedded native visualizer window is never re-created; all other views
 * load/unload through Loaders.
 *
 * @version 3.0.0 — Nav Rail App Shell
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

    // Flush any pending auto-save on close
    onClosing: SettingsBridge.save()

    readonly property var viewMeta: {
        "library":    { label: "Library" },
        "listen":     { label: "Listen" },
        "canvas":     { label: "Canvas" },
        "studio":     { label: "Studio" },
        "automation": { label: "Automation" },
        "settings":   { label: "Settings" }
    }

    // ── Active view (persisted via expandedPanel) ────────────────
    property string activeView: "library"
    onViewChanged: SettingsBridge.expandedPanel = activeView

    function navigate(viewId) {
        if (viewMeta.hasOwnProperty(viewId))
            activeView = viewId
    }

    // ── Rail expansion (persisted via sidebarWidth; >100 = expanded) ──
    property bool railUserExpanded: true
    readonly property bool railEffectiveExpanded:
        railUserExpanded && width >= Theme.navRailAutoCollapseBelow

    function setRailExpanded(expanded) {
        railUserExpanded = expanded
        SettingsBridge.sidebarWidth = expanded ? Theme.navRailWidthExpanded
                                               : Theme.navRailWidthCollapsed
    }

    Component.onCompleted: {
        const savedView = SettingsBridge.expandedPanel
        activeView = viewMeta.hasOwnProperty(savedView) ? savedView : "library"
        railUserExpanded = SettingsBridge.sidebarWidth > 100
    }

    title: {
        var t = "ChadVis"
        if (AudioBridge.currentTrack.title)
            t = AudioBridge.currentTrack.artist + " - " + AudioBridge.currentTrack.title + " | " + t
        if (RecordingBridge.isRecording)
            t = "⏺ " + t
        return t
    }

    background: Rectangle { color: Theme.background }

    // ══════════════════════════════════════════════
    // TOP BAR
    // ══════════════════════════════════════════════
    header: ToolBar {
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
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingSmall
            spacing: Theme.spacingMedium

            Text {
                text: mainWindow.viewMeta[mainWindow.activeView].label
                color: Theme.accent
                font: Theme.fontSubtitle
            }

            Rectangle {
                width: 1
                height: Theme.topBarHeight - 16
                color: Theme.border
            }

            // Now playing (compact ticker)
            RowLayout {
                spacing: Theme.spacingSmall
                Layout.fillWidth: true

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
                    text: AudioBridge.currentTrack.artist ? "• " + AudioBridge.currentTrack.artist : ""
                    color: Theme.textSecondary
                    font: Theme.fontBody
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
            }

            // Recording chip
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
                        width: 7
                        height: 7
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

    // ══════════════════════════════════════════════
    // FOOTER STATUS BAR
    // ══════════════════════════════════════════════
    footer: ToolBar {
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
                text: "v2.0.0 • Refactor Edition"
                color: Theme.textSecondary
                font: Theme.fontCaption
            }
        }
    }

    // ══════════════════════════════════════════════
    // SHELL BODY: NAV RAIL + VIEW HOST
    // ══════════════════════════════════════════════
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

            onNavigate: function(viewId) { mainWindow.navigate(viewId) }
            onExpandToggled: mainWindow.setRailExpanded(!mainWindow.railUserExpanded)
        }

        // View host — everything to the right of the rail.
        // Every surface fades in via an explicit opacity Behavior.
        Item {
            id: viewHost
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.left: navRail.right
            clip: true

            // ── LISTEN: lives forever (owns the native visualizer window) ──
            ListenView {
                anchors.fill: parent
                visible: mainWindow.activeView === "listen"
                opacity: visible ? 1.0 : 0.0
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutQuad }
                }
            }

            // ── LIBRARY: default landing view ──
            Loader {
                anchors.fill: parent
                active: mainWindow.activeView === "library"
                visible: active
                opacity: visible ? 1.0 : 0.0
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutQuad }
                }
                sourceComponent: LibraryView {}
            }

            // ── Roadmap stubs ──
            Loader {
                anchors.fill: parent
                active: mainWindow.activeView === "canvas"
                visible: active
                opacity: visible ? 1.0 : 0.0
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutQuad }
                }
                sourceComponent: ComingSoonPage {
                    pageTitle: "Canvas"
                    glyph: "◫"
                    blurb: "Brand every frame: layer text, imagery and karaoke captions over the live projectM texture, then keyframe the lot."
                    milestones: [
                        "Scene model with element stack & render-time binding",
                        "Keyframe timeline with easing curves",
                        "One compositing path shared by preview and export"
                    ]
                }
            }

            Loader {
                anchors.fill: parent
                active: mainWindow.activeView === "studio"
                visible: active
                opacity: visible ? 1.0 : 0.0
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutQuad }
                }
                sourceComponent: ComingSoonPage {
                    pageTitle: "Studio"
                    glyph: "♫"
                    blurb: "The creation suite: prompt, style and seed control with full client-side overrides over the captured v2-web endpoint."
                    milestones: [
                        "Generation surface with persona presets",
                        "B-side orchestrator workspaces",
                        "Lyrics assist & cover-art adapters"
                    ]
                }
            }

            Loader {
                anchors.fill: parent
                active: mainWindow.activeView === "automation"
                visible: active
                opacity: visible ? 1.0 : 0.0
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutQuad }
                }
                sourceComponent: ComingSoonPage {
                    pageTitle: "Automation"
                    glyph: "⌁"
                    blurb: "Batch music-video rendering at scale: durable runs, crash-safe resume and per-item failure isolation."
                    milestones: [
                        "Durable run queue with input snapshots",
                        "Resume interrupted runs",
                        "Named encoder presets (youtube1080p60, discord8mb…)"
                    ]
                }
            }

            // ── SETTINGS ──
            Loader {
                anchors.fill: parent
                active: mainWindow.activeView === "settings"
                visible: active
                opacity: visible ? 1.0 : 0.0
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutQuad }
                }
                sourceComponent: SettingsView {}
            }
        }
    }

    // ══════════════════════════════════════════════
    // KEYBOARD SHORTCUTS
    // Sequences are bound to the KeyboardConfig values exposed by
    // SettingsBridge so the config file remains the single source of truth.
    // NOTE: the native VisualizerWindow also honors nextPreset/prevPreset/
    // toggleFullscreen keys when the visualizer itself has input focus;
    // these QML shortcuts cover the case where the main window has focus.
    // ══════════════════════════════════════════════

    Shortcut {
        sequence: SettingsBridge.keyboardPlayPause
        onActivated: AudioBridge.togglePlayPause()
    }

    Shortcut {
        sequence: SettingsBridge.keyboardNextTrack
        onActivated: AudioBridge.next()
    }

    Shortcut {
        sequence: SettingsBridge.keyboardPrevTrack
        onActivated: AudioBridge.previous()
    }

    // TODO(fullscreen): no QML-invokable fullscreen toggle exists yet.
    // VisualizerWindow::toggleFullscreen() is not Q_INVOKABLE and
    // VisualizerBridge exposes no fullscreen slot, so this handler cannot
    // be wired without a C++ change (e.g. add Q_INVOKABLE toggleFullscreen()
    // to VisualizerBridge forwarding to the window).
    Shortcut {
        sequence: SettingsBridge.keyboardToggleFullscreen
        onActivated: console.log("Fullscreen toggle (TODO)")
    }

    Shortcut {
        sequence: SettingsBridge.keyboardToggleRecord
        onActivated: {
            if (RecordingBridge.isRecording) {
                RecordingBridge.stopRecording()
            } else {
                RecordingBridge.startRecording()
            }
        }
    }

    Shortcut {
        sequence: SettingsBridge.keyboardNextPreset
        onActivated: VisualizerBridge.nextPreset()
    }

    Shortcut {
        sequence: SettingsBridge.keyboardPrevPreset
        onActivated: VisualizerBridge.previousPreset()
    }

    // Toggles the nav rail between icons-only and icon+label widths.
    // No dedicated KeyboardConfig key yet; hardcoded like the old drawer
    // toggle until one is added.
    Shortcut {
        sequence: "M"
        onActivated: mainWindow.setRailExpanded(!mainWindow.railUserExpanded)
    }
}
