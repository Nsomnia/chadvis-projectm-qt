/**
 * @file main.qml
 * @brief Suno-first desktop shell with a persistent projectM Video surface
 *
 * Navigation is Library → Explore → Create → Listen → Video → Settings. Settings is
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
        "notifications": { label: "Notifications" },
        "discover": { label: "Explore" },
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

    // ── Fullscreen ─────────────────────────────────────────────────────────
    // Fullscreen is a property of THIS QQuickWindow, not of the native
    // projectM QWindow.  VisualizerWindow is never shown as a top-level
    // window — nothing in the C++ ever calls show() on it, it exists only as
    // a QQuickWindowContainer child (VideoView.qml:18) — so QWindow's
    // showFullScreen() on it is inert, and the nav rail, header, footer,
    // overlay and karaoke layers are QML siblings in this scene that would
    // stay on screen no matter what the native window claimed.  Fullscreening
    // this ApplicationWindow is the only definition that both hides the
    // chrome and actually fills the display.
    //
    // Derived from `visibility` rather than stored in a flag, so a
    // window-manager-initiated exit (macOS Escape, the green button) cannot
    // leave the shell believing it is still fullscreen and refusing to
    // re-enter on the next press.
    readonly property bool fullscreenActive: visibility === Window.FullScreen

    readonly property var contentViews: ["library", "notifications", "discover",
                                        "create", "listen", "video"]

    // Geometry is captured on the way in and re-applied on the way out rather
    // than trusted to the platform: Qt does not guarantee that leaving
    // fullscreen restores the pre-fullscreen size, and this app is
    // user-resizable, so a lost size would be a permanently squashed window.
    property string viewBeforeFullscreen: ""
    property real restoreX: 0
    property real restoreY: 0
    property real restoreWidth: 0
    property real restoreHeight: 0

    function setFullscreen(on) {
        if (on === fullscreenActive)
            return

        if (on) {
            viewBeforeFullscreen = activeView
            restoreX = x
            restoreY = y
            restoreWidth = width
            restoreHeight = height
            // Fullscreen means "the visualizer, full bleed", so reveal the
            // surface it belongs to. This is the one behaviour the previous
            // placeholder got right and it is kept deliberately: a fullscreen
            // Library would be the app shell at screen size with no
            // visualizer in it.
            navigate("video")
            visibility = Window.FullScreen
        } else {
            visibility = Window.Windowed
            x = restoreX
            y = restoreY
            width = restoreWidth
            height = restoreHeight
            // Round-trip back to wherever the user was, mirroring the
            // returnView idiom the Settings window already uses. Guarded on
            // contentViews so a stale "settings" can never re-open a window.
            if (contentViews.indexOf(viewBeforeFullscreen) >= 0
                    && activeView === "video")
                navigate(viewBeforeFullscreen)
            viewBeforeFullscreen = ""
        }
    }

    // A bare-letter shortcut must not fire while the user is typing, or every
    // "f" typed into the Library search box would fullscreen the app. Window
    // exposes activeFocusItem (QQuickWindowQmlImpl prototypes QQuickWindow);
    // the short ancestor walk covers both cases, where a TextField takes focus
    // itself and where focus lands on the TextInput/TextEdit inside a
    // TextField, TextArea or SpinBox contentItem.
    readonly property bool textEntryFocused: {
        var item = activeFocusItem
        for (var depth = 0; item && depth < 4; ++depth) {
            if (item instanceof TextInput || item instanceof TextEdit
                    || item instanceof TextField || item instanceof TextArea)
                return true
            item = item.parent
        }
        return false
    }

    // Only a single unmodified character can be typed by accident. A
    // modifier-bound or function key is never swallowed by a text field, so
    // it keeps working everywhere.
    readonly property bool fullscreenKeyConflictsWithTyping:
        /^[A-Za-z0-9]$/.test(String(SettingsBridge.keyboardToggleFullscreen).trim())

    Component.onCompleted: {
        const savedView = String(SettingsBridge.expandedPanel)
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
        // Fullscreen drops the shell chrome; the explicit implicitHeight
        // collapse keeps the space freed even on platforms where
        // ApplicationWindow's own layout still reserves an invisible bar.
        visible: !mainWindow.fullscreenActive
        implicitHeight: mainWindow.fullscreenActive ? 0 : Theme.topBarHeight

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
        visible: !mainWindow.fullscreenActive
        implicitHeight: mainWindow.fullscreenActive ? 0 : Theme.statusBarHeight

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
                text: "v" + SettingsBridge.version + " · Suno Desktop"
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

            visible: !mainWindow.fullscreenActive

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
            // The rail's width is reserved only while it is on screen;
            // fullscreen reclaims the strip for the visualizer.
            anchors.left: mainWindow.fullscreenActive ? parent.left : navRail.right
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

            NotificationsView {
                anchors.fill: parent
                visible: mainWindow.activeView === "notifications"
                opacity: visible ? 1 : 0
                onNavigateRequested: function(viewId) {
                    mainWindow.navigate(viewId)
                }
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.durationNormal
                        easing.type: Easing.OutCubic
                    }
                }
            }

            DiscoverView {
                anchors.fill: parent
                visible: mainWindow.activeView === "discover"
                opacity: visible ? 1 : 0
                onNavigateRequested: function(viewId) {
                    mainWindow.navigate(viewId)
                }
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

    // ── Fullscreen: two bindings, one action ──────────────────────────────
    // DECISION (do not re-litigate without reading this):
    //
    //  * Fullscreen is the QML ApplicationWindow, NOT the native projectM
    //    QWindow.  VisualizerWindow is never a top-level window (no C++ path
    //    calls show() on it) — it lives only as a QQuickWindowContainer child,
    //    so a QWindow::showFullScreen() request on it is inert, and it is never
    //    the activated window so its keyPressEvent can never see a key.  It
    //    would also leave the nav rail, header, footer, overlay and karaoke
    //    layers on screen, because those are QML siblings in this scene.  That
    //    is not fullscreen, that is a window that lied.
    //
    //    The C++ half of that (VisualizerWindow::toggleFullscreen, its
    //    fullscreen_ / normalGeometry_ state, and the left-double-click that
    //    drove it) has since been DELETED as dead: the double-click was the one
    //    path that really did fire, and it was not harmless — it latched
    //    fullscreen_ = true forever and fought the container's layout sync while
    //    the QML chrome stayed on screen anyway.  This comment is the only
    //    record of why; the class-level @section Embedding in
    //    VisualizerWindow.hpp is the other half.  Note the double-click gesture
    //    is therefore gone — F / F11 is the supported way in.
    //
    //  * F and F11 are two bindings for the SAME action, not a primary and a
    //    fallback.  F is the user-configurable key (KeyboardConfig
    //    toggleFullscreen, default "F") and is suppressed during text entry
    //    because a bare letter is typeable.  F11 is unconditional, because a
    //    text field can never consume a function key as text — that is what
    //    makes the shortcut still work when the user is typing.  Neither can
    //    double-fire: they are distinct key events, and there is no longer a
    //    native F/F11 branch to collide with (see above).
    //
    //  * The "reveal the Video page" step is kept, but as a precondition of
    //    the action rather than a leftover: fullscreen is defined as "the
    //    visualizer, full bleed", and setFullscreen() now also returns to the
    //    previous view on the way out.
    Shortcut {
        sequence: SettingsBridge.keyboardToggleFullscreen
        enabled: !settingsWindow.visible
                 && (!mainWindow.fullscreenKeyConflictsWithTyping
                     || !mainWindow.textEntryFocused)
        onActivated: mainWindow.setFullscreen(!mainWindow.fullscreenActive)
    }

    Shortcut {
        sequence: "F11"
        enabled: !settingsWindow.visible
        onActivated: mainWindow.setFullscreen(!mainWindow.fullscreenActive)
    }

    // Escape is bound only while fullscreen, so it cannot shadow the Escape
    // handling of a dialog, a sheet or a menu outside fullscreen.
    Shortcut {
        sequence: "Escape"
        enabled: !settingsWindow.visible && mainWindow.fullscreenActive
        onActivated: mainWindow.setFullscreen(false)
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
