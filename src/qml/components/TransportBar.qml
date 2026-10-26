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

    property bool compact: false

    /// Disables every transport control when the queue has nothing selected.
    /// Volume and "open file" stay live: they are useful before a track exists.
    /// Bound through the title because that is what every consumer of
    /// AudioBridge.currentTrack already reads, and an empty queue reports one.
    readonly property bool hasTrack: AudioBridge.currentTrack
                                     && AudioBridge.currentTrack.title !== ""

    /// Last recording failure, shown in the bar rather than only on the Video
    /// tab's Record panel: the button that fails may have been pressed from
    /// anywhere, and a failure nobody can see is the silent-failure class this
    /// project treats as a defect.
    property string recordError: ""

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
            enabled: root.hasTrack

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
            visible: !root.compact
            Layout.preferredWidth: 1
            Layout.preferredHeight: parent.height - 20
            color: Theme.border
        }

        // ── Seek + times ───────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            AppSlider {
                id: seekSlider
                Layout.fillWidth: true
                Layout.minimumWidth: 90
                enabled: root.hasTrack
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
                    visible: !root.compact
                    text: AudioBridge.currentTrack.title || "No Track Selected"
                    color: Theme.textPrimaryVariant
                    font: Theme.fontCaptionStrong
                    elide: Text.ElideRight
                    Layout.preferredWidth: 180
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
            visible: !root.compact
            Layout.preferredWidth: 1
            Layout.preferredHeight: parent.height - 20
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
                visible: !root.compact
                Layout.preferredWidth: 110
                from: 0
                to: 100
                value: AudioBridge.volume * 100
                onMoved: AudioBridge.setVolume(value / 100)
            }
        }

        Rectangle {
            visible: !root.compact
            Layout.preferredWidth: 1
            Layout.preferredHeight: parent.height - 20
            color: Theme.border
        }

        // ── Record ─────────────────────────────────
        // Recording belongs here, next to play, because a music video is the
        // product and this bar is the only transport that exists on every view.
        // Before this, the sole start button lived three clicks deep on the
        // Video tab's dock, so a user playing a track had no visible way to
        // capture it -- reported 2026-10-26 with the app running and a track
        // playing.
        RowLayout {
            spacing: Theme.spacingSmall

            Text {
                visible: root.recordError.length > 0 && !RecordingBridge.isRecording
                text: root.recordError
                color: Theme.error
                font: Theme.fontCaption
                elide: Text.ElideRight
                Layout.maximumWidth: 240
                ToolTip.visible: errorHover.hovered
                ToolTip.text: root.recordError
                ToolTip.delay: 300
                HoverHandler { id: errorHover }
            }

            Text {
                visible: RecordingBridge.isRecording
                text: RecordingBridge.recordingTime
                color: Theme.recording
                font: Theme.fontCaptionStrong
            }

            AppButton {
                icon: RecordingBridge.isRecording
                      ? "qrc:/qt/qml/ChadVis/resources/icons/stop.svg"
                      : "qrc:/qt/qml/ChadVis/resources/icons/record.svg"
                highlighted: RecordingBridge.isRecording
                implicitWidth: 40
                implicitHeight: 40
                ToolTip.visible: hovered
                ToolTip.text: RecordingBridge.isRecording
                              ? "Stop and write the file"
                              : "Record the visualizer and the playing audio"
                ToolTip.delay: 300
                onClicked: {
                    if (RecordingBridge.isRecording)
                        RecordingBridge.stopRecording()
                    else
                        RecordingBridge.startRecording()
                }
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

    Connections {
        target: RecordingBridge

        // One error surface for the whole bar. Cleared when a recording
        // actually starts, so a stale refusal does not sit next to a running REC.
        function onRecordingError(message) { root.recordError = message }

        function onRecordingStateChanged() {
            if (RecordingBridge.isRecording)
                root.recordError = ""
        }
    }

    FileDialog {
        id: fileDialog
        title: "Open Audio File"
        nameFilters: ["Audio Files (*.mp3 *.flac *.wav *.ogg *.m4a *.aac)"]
        onAccepted: AudioBridge.loadFile(root.toLocalFilePath(selectedFile))
    }
}
