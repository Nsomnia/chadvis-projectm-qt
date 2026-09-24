pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
import ChadVis
import "../components"
import "../panels"

Item {
    id: root

    readonly property int dockWidth: Math.min(360, Math.max(300, width * 0.34))
    property bool dockOpen: true
    property int activeTab: 0

    WindowContainer {
        id: visualizerContainer
        anchors.fill: parent
        window: VisualizerBridge.visualizerWindow
        visible: VisualizerBridge.visualizerWindow !== null
    }

    VisualizerOverlay {
        anchors.fill: parent
    }

    KaraokeMaster {
        anchors.fill: parent
        visible: SettingsBridge.karaokeEnabled
        accentColor: Theme.accent
        showGlow: true
        verticalPosition: SettingsBridge.karaokeYPosition
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.withAlpha(Theme.background, 0)
        border.color: Theme.border
        border.width: 1
        visible: !RecordingBridge.isRecording
    }

    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: Theme.spacingMedium
        text: "projectM v4 · " + (AudioBridge.isPlaying ? "Playing" : "Ready")
        color: Theme.textPrimary
        font: Theme.fontCaption
        opacity: 0.55
    }

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
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
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

    AppButton {
        id: drawerHandle
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 34
        height: 72
        implicitWidth: 34
        implicitHeight: 72
        flat: true
        icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
        buttonRadius: Theme.radiusMedium
        rotation: root.dockOpen ? 180 : 0
        opacity: root.dockOpen ? 0 : 1
        visible: opacity > 0.01
        z: 20
        onClicked: root.dockOpen = true
        ToolTip.visible: hovered
        ToolTip.text: "Open video tools"
        ToolTip.delay: 350

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast }
        }

        Behavior on rotation {
            NumberAnimation {
                duration: Theme.durationNormal
                easing.type: Easing.InOutCubic
            }
        }
    }

    Rectangle {
        id: sideDock
        z: 10
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: root.dockOpen ? root.width - width : root.width
        width: root.dockWidth
        color: Theme.withAlpha(Theme.surface, 0.97)

        Behavior on x {
            NumberAnimation {
                duration: Theme.durationNormal
                easing.type: Easing.InOutCubic
            }
        }

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

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                color: Theme.backgroundAlt

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingTiny
                    spacing: Theme.spacingTiny

                    Repeater {
                        model: [
                            { label: "FX", tab: 0 },
                            { label: "Presets", tab: 1 },
                            { label: "Record", tab: 2 }
                        ]

                        delegate: AppButton {
                            required property var modelData

                            Layout.fillWidth: true
                            text: modelData.label
                            flat: root.activeTab !== modelData.tab
                            highlighted: root.activeTab === modelData.tab
                            implicitHeight: 30
                            buttonRadius: Theme.radiusSmall
                            onClicked: root.activeTab = modelData.tab
                        }
                    }

                    AppButton {
                        icon: "qrc:/qt/qml/ChadVis/resources/icons/clear.svg"
                        flat: true
                        implicitWidth: 28
                        implicitHeight: 28
                        onClicked: root.dockOpen = false
                        ToolTip.visible: hovered
                        ToolTip.text: "Hide video tools"
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.activeTab

                Item {
                    OverlayPanel {
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
                    RecordingPanel {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                    }
                }
            }
        }
    }
}
