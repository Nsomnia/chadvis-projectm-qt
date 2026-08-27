/**
 * @file TransportBar.qml
 * @brief Floating glassmorphism playback transport for the Listen view
 *
 * Absorbs the old PlaybackPanel controls (open file, transport cluster,
 * seek, volume) into a single horizontal glass bar that floats over the
 * projectM visualizer. Translucent so the canvas glows through.
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import ChadVis

Rectangle {
    id: root

    // QML's url value type has no toLocalFile(); replicate QUrl::toLocalFile
    // (strip scheme, percent-decode, drop slash before Windows drive letter).
    function toLocalFilePath(url) {
        const s = url.toString()
        if (!s.startsWith("file://"))
            return s
        let path = decodeURIComponent(s.slice("file://".length))
        if (/^\/[A-Za-z]:\//.test(path))
            path = path.slice(1)
        return path
    }

    implicitHeight: 64
    radius: Theme.radiusLarge

    color: Theme.glassBackground
    border.width: 1
    border.color: RecordingBridge.isRecording ? Theme.recording : Theme.glassBorder

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // ── Transport cluster ──────────────────────
        RowLayout {
            spacing: Theme.spacingSmall

            AppButton {
                icon: "qrc:/qt/qml/ChadVis/resources/icons/prev.svg"
                flat: true
                implicitWidth: 36
                implicitHeight: 36
                onClicked: AudioBridge.previous()
            }

            AppButton {
                icon: AudioBridge.isPlaying ? "qrc:/qt/qml/ChadVis/resources/icons/pause.svg"
                                            : "qrc:/qt/qml/ChadVis/resources/icons/play.svg"
                highlighted: AudioBridge.isPlaying
                implicitWidth: 44
                implicitHeight: 44
                onClicked: AudioBridge.togglePlayPause()
            }

            AppButton {
                icon: "qrc:/qt/qml/ChadVis/resources/icons/stop.svg"
                flat: true
                implicitWidth: 36
                implicitHeight: 36
                onClicked: AudioBridge.stop()
            }

            AppButton {
                icon: "qrc:/qt/qml/ChadVis/resources/icons/next.svg"
                flat: true
                implicitWidth: 36
                implicitHeight: 36
                onClicked: AudioBridge.next()
            }
        }

        Rectangle {
            width: 1
            height: parent.height - 20
            color: Theme.border
        }

        // ── Seek + times ───────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            AppSlider {
                id: seekSlider
                Layout.fillWidth: true
                from: 0
                to: AudioBridge.duration || 1
                value: AudioBridge.position
                onMoved: AudioBridge.seek(value)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Text {
                    text: Theme.formatTime(AudioBridge.position)
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: AudioBridge.currentTrack.title || "No Track Selected"
                    color: Theme.textPrimaryVariant
                    font: Theme.fontCaptionStrong
                    elide: Text.ElideRight
                    Layout.maximumWidth: 260
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: Theme.formatTime(AudioBridge.duration)
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                }
            }
        }

        Rectangle {
            width: 1
            height: parent.height - 20
            color: Theme.border
        }

        // ── Volume ─────────────────────────────────
        RowLayout {
            spacing: Theme.spacingSmall

            AppButton {
                icon: AudioBridge.volume > 0 ? "qrc:/qt/qml/ChadVis/resources/icons/volume-high.svg"
                                             : "qrc:/qt/qml/ChadVis/resources/icons/volume-mute.svg"
                flat: true
                implicitWidth: 32
                implicitHeight: 32
                onClicked: AudioBridge.setVolume(AudioBridge.volume > 0 ? 0 : 0.5)
            }

            AppSlider {
                Layout.preferredWidth: 110
                from: 0
                to: 100
                value: AudioBridge.volume * 100
                onMoved: AudioBridge.setVolume(value / 100)
            }
        }

        // ── Open file ──────────────────────────────
        AppButton {
            icon: "qrc:/qt/qml/ChadVis/resources/icons/plus.svg"
            flat: true
            implicitWidth: 32
            implicitHeight: 32
            ToolTip.visible: hovered
            ToolTip.text: "Open audio file…"
            ToolTip.delay: 400
            onClicked: fileDialog.open()
        }
    }

    FileDialog {
        id: fileDialog
        title: "Open Audio File"
        nameFilters: ["Audio Files (*.mp3 *.flac *.wav *.ogg *.m4a *.aac)"]
        onAccepted: AudioBridge.loadFile(root.toLocalFilePath(selectedFile))
    }
}
