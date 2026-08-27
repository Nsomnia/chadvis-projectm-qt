/**
 * @file ListenView.qml
 * @brief Player + visualizer combined home view
 *
 * The native projectM window (via WindowContainer) dominates the surface;
 * a translucent glass TransportBar floats bottom-center and a right-hand
 * workbench drawer slides in for queue/presets/lyrics/overlays/recording
 * — everything the old accordion sidebar exposed, one keystroke from the
 * canvas without permanently stealing its space.
 *
 * NOTE: the embedded QWindow lives here for the whole app lifetime
 * (ListenView is never unloaded by the shell) so the GL context is not
 * churned across view switches. Hidden = not exposed = render loop idles,
 * which is exactly what we want while browsing the Library.
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window
import ChadVis
import "../components"
import "../panels"

Item {
    id: root

    // ── Workbench drawer state ──────────────────────
    property bool drawerOpen: false
    readonly property int dockWidth: 340

    // Active workbench tab lives at root scope: the tab strip is declared
    // before the StackLayout it selects, so the buttons must not reference
    // a not-yet-created id during initial binding evaluation.
    property int activeTab: 0

    // ════════════════════════════════════════════════
    // VISUALIZER CANVAS (native window)
    // ════════════════════════════════════════════════
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

    KaraokeMaster {
        id: karaokeMaster
        anchors.fill: parent
        accentColor: Theme.accent
        // Bound to persisted settings; glow toggle remains a local
        // KaraokeSettings.qml preference until it gains a config key.
        showGlow: true
        verticalPosition: SettingsBridge.karaokeYPosition
    }

    // Subtle frame around the canvas (kept from the previous shell)
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.border
        border.width: 1
        visible: !RecordingBridge.isRecording
    }

    // Canvas caption (relocated above the transport bar's old spot)
    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: Theme.spacingMedium
        text: "projectM v4 • " + (AudioBridge.isPlaying ? "Playing" : "Ready")
        color: Theme.textPrimary
        font: Theme.fontCaption
        opacity: 0.45
    }

    // Recording badge floats over canvas when active
    Rectangle {
        visible: RecordingBridge.isRecording
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacingMedium
        implicitWidth: recRow.implicitWidth + Theme.spacingMedium
        implicitHeight: 28
        radius: Theme.radiusSmall
        color: Theme.recording

        RowLayout {
            id: recRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            PulseIndicator {
                width: 8
                height: 8
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

    // ════════════════════════════════════════════════
    // FLOATING TRANSPORT BAR
    // ════════════════════════════════════════════════
    TransportBar {
        id: transportBar
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLarge
        width: Math.min(860, parent.width - Theme.spacingXL * 2)

        // Horizontally centered over the canvas; nudges left when the
        // workbench is open so it stays centered on the remaining space.
        x: root.drawerOpen
           ? (root.width - root.dockWidth - width) / 2
           : (root.width - width) / 2
        Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutCubic } }
    }

    // ════════════════════════════════════════════════
    // WORKBENCH DRAWER HANDLE
    // ════════════════════════════════════════════════
    Rectangle {
        id: drawerHandle

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 26
        height: 72
        radius: Theme.radiusMedium
        color: root.drawerOpen ? Theme.surfaceOverlay : Theme.glassBackground
        border.width: 1
        border.color: drawerHandleMouse.containsMouse ? Theme.accent : Theme.glassBorder

        // Keep the handle reachable even when the dock is open
        // (dock itself uses z: 10; hardcoded to avoid a forward-id reference)
        z: 20
        opacity: root.drawerOpen ? 0.0 : 1.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

        Image {
            anchors.centerIn: parent
            source: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
            rotation: 180
            sourceSize: Qt.size(14, 14)
            width: 14
            height: 14
            fillMode: Image.PreserveAspectFit
            layer.enabled: true
            layer.effect: MultiEffect {
                autoPaddingEnabled: true
                colorization: 1.0
                colorizationColor: Theme.textSecondary
            }
        }

        MouseArea {
            id: drawerHandleMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.drawerOpen = true
        }
    }

    // ════════════════════════════════════════════════
    // WORKBENCH DRAWER
    // ════════════════════════════════════════════════
    Rectangle {
        id: sideDock

        z: 10

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: root.drawerOpen ? root.width - width : root.width
        width: root.dockWidth
        Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutCubic } }

        color: Theme.withAlpha(Theme.surface, 0.96)

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.border
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ── Tab strip ───────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                color: Theme.backgroundAlt

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingTiny
                    spacing: 0

                    Repeater {
                        model: [
                            { label: "Queue",   tab: 0 },
                            { label: "Presets", tab: 1 },
                            { label: "Lyrics",  tab: 2 },
                            { label: "FX",      tab: 3 },
                            { label: "Rec",     tab: 4 }
                        ]

                        delegate: AppButton {
                            id: workbenchTabButton

                            property bool active: root.activeTab === modelData.tab

                            text: modelData.label
                            flat: !active
                            highlighted: active
                            implicitHeight: 30
                            buttonRadius: Theme.radiusSmall
                            onClicked: root.activeTab = modelData.tab
                        }
                    }

                    Item { Layout.fillWidth: true }

                    AppButton {
                        icon: "qrc:/qt/qml/ChadVis/resources/icons/clear.svg"
                        flat: true
                        implicitWidth: 28
                        implicitHeight: 28
                        ToolTip.visible: hovered
                        ToolTip.text: "Close workbench"
                        ToolTip.delay: 400
                        onClicked: root.drawerOpen = false
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            // ── Panel stack ─────────────────────────
            StackLayout {
                id: workbenchTabs
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.activeTab

                Item {
                    PlaylistPanel {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                    }
                }

                Item {
                    PresetsPanel {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                    }
                }

                Item {
                    LyricsPanel {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                    }
                }

                Item {
                    OverlayPanel {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                    }
                }

                Item {
                    RecordingPanel {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                    }
                }
            }
        }
    }
}
