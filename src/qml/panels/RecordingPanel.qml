import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import ChadVis
import "../components"

ColumnLayout {
    id: root

    spacing: Theme.spacingMedium

    // One status line for the whole panel, fed by both bridges that can fail in
    // it. It used to not exist at all: RecordingBridge::recordingError had zero
    // QML consumers, so a failed start or stop was logged and never seen. The
    // burn-in pass adds a second, slower operation to the same panel, so it
    // shares the line rather than growing a second one that could contradict it.
    property string statusText: ""
    property bool statusIsError: false

    // A file path in this panel is longer than the dock is wide, so the visible
    // text names the file and the tooltip carries the whole path.
    function fileName(path) {
        if (!path)
            return ""
        const parts = path.split(/[\\/]/)
        return parts.length > 0 ? parts[parts.length - 1] : path
    }

    // One place, because a string the panel states twice is a string that will
    // eventually be stated two different ways.
    readonly property string burnInDescription: RecordingBridge.burnInInputFile.length === 0
        ? ""
        : fileName(RecordingBridge.burnInInputFile) + " + "
          + fileName(RecordingBridge.burnInSubtitleFile)
          + " → " + fileName(RecordingBridge.burnInOutputFile)

    // QML's url value type has no toLocalFile(), so replicate
    // QUrl::toLocalFile here: strip the scheme, percent-decode, and drop the
    // extra slash before a Windows drive letter ("file:///C:/x" -> "C:/x").
    function toLocalFilePath(url) {
        const s = url.toString()
        if (!s.startsWith("file://"))
            return s
        let path = decodeURIComponent(s.slice("file://".length))
        if (/^\/[A-Za-z]:\//.test(path))
            path = path.slice(1)
        return path
    }

    // ═══════════════════════════════════════════════════════════
    // HEADER (Removed redundant ToolBar since it's in an accordion)
    // ═══════════════════════════════════════════════════════════

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMedium

        AppButton {
            id: recordButton
            text: RecordingBridge.isRecording ? "Stop" : "Record"
            icon: RecordingBridge.isRecording ? "qrc:/qt/qml/ChadVis/resources/icons/stop.svg" : "qrc:/qt/qml/ChadVis/resources/icons/record.svg"
            // FIX: was inverted (`!isRecording`) — active recording now
            // shows the emphasized/highlighted state.
            highlighted: RecordingBridge.isRecording
            onClicked: {
                if (RecordingBridge.isRecording) {
                    RecordingBridge.stopRecording()
                } else {
                    RecordingBridge.startRecording(outputPathField.text)
                }
            }
        }

        Label {
            visible: RecordingBridge.isRecording
            text: RecordingBridge.recordingTime
            color: Theme.error
            font: Theme.fontSubtitle
        }

        Item { Layout.fillWidth: true }

        // Pulse indicator
        PulseIndicator {
            active: RecordingBridge.isRecording
            baseColor: Theme.recording
            size: 12
        }
    }

    // ═══════════════════════════════════════════════════════════
    // STATUS
    // ═══════════════════════════════════════════════════════════

    Text {
        Layout.fillWidth: true
        visible: root.statusText.length > 0
        text: root.statusText
        color: root.statusIsError ? Theme.error : Theme.success
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    Connections {
        target: RecordingBridge

        function onRecordingError(message) {
            root.statusText = message
            root.statusIsError = true
        }

        function onBurnInFinished(path) {
            root.statusText = "Burned in — wrote " + root.fileName(path)
            root.statusIsError = false
        }
    }

    Connections {
        target: LyricsBridge

        // The sidecar's arrival is the only thing that can unblock the burn-in
        // button, and the bridge learns it from the filesystem. Re-notifying here
        // enables the button from the event that created the file instead of on
        // the next unrelated repaint.
        function onExportFinished(path) {
            root.statusText = "Saved " + root.fileName(path)
            root.statusIsError = false
            RecordingBridge.refreshBurnInState()
        }

        function onExportFailed(message) {
            root.statusText = message
            root.statusIsError = true
        }
    }

    // ═══════════════════════════════════════════════════════════
    // SETTINGS GRID
    // ═══════════════════════════════════════════════════════════

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.spacingMedium
        rowSpacing: Theme.spacingSmall

        Label {
            text: "Codec"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        AppComboBox {
            id: codecCombo
            Layout.fillWidth: true
            model: ["libx264", "libx265", "libvpx-vp9", "h264_nvenc", "hevc_nvenc"]
            currentIndex: {
                const index = model.indexOf(RecordingBridge.videoCodec)
                return index >= 0 ? index : 0
            }
            onActivated: RecordingBridge.videoCodec = currentText
            contentFont: Theme.fontBody
        }

        Label {
            text: "Quality (CRF)"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        RowLayout {
            Layout.fillWidth: true
            AppSlider {
                id: qualitySlider
                Layout.fillWidth: true
                from: 0
                to: 51
                value: SettingsBridge.recorderCrf
                onMoved: (val) => SettingsBridge.recorderCrf = Math.round(val)
            }
            Label {
                text: Math.round(qualitySlider.value)
                color: Theme.textPrimary
                font: Theme.fontCaption
                Layout.preferredWidth: 20
            }
        }

        Label {
            text: "Output Path"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            AppTextField {
                id: outputPathField
                Layout.fillWidth: true
                placeholderText: "Auto-generated if empty"
                color: Theme.textPrimary
                font: Theme.fontBody
                padding: Theme.spacingSmall
            }

            AppButton {
                icon: "qrc:/qt/qml/ChadVis/resources/icons/plus.svg"
                implicitWidth: 32
                implicitHeight: 32
                onClicked: fileDialog.open()
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // LIVE STATS (Visible only when recording)
    // ═══════════════════════════════════════════════════════════

    Rectangle {
        Layout.fillWidth: true
        visible: RecordingBridge.isRecording
        height: statsLayout.implicitHeight + Theme.spacingMedium * 2
        color: Theme.backgroundAlt
        radius: Theme.radiusSmall
        border.color: Theme.border

        ColumnLayout {
            id: statsLayout
            anchors.fill: parent
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Frames"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
                Text { text: RecordingBridge.framesWritten; color: Theme.textPrimary; font: Theme.fontCaptionStrong }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Size"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
                Text { text: RecordingBridge.fileSize; color: Theme.textPrimary; font: Theme.fontCaptionStrong }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "FPS"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
                Text { text: RecordingBridge.encodeFps; color: Theme.textPrimary; font: Theme.fontCaptionStrong }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Text { text: "Buffer"; color: Theme.textSecondary; font: Theme.fontCaption }
                Rectangle {
                    Layout.fillWidth: true
                    height: 4
                    radius: 2
                    color: Theme.surface
                    Rectangle {
                        width: parent.width * (RecordingBridge.bufferHealth / 100)
                        height: parent.height
                        radius: parent.radius
                        color: RecordingBridge.bufferHealth > 70 ? Theme.success : (RecordingBridge.bufferHealth > 30 ? Theme.warning : Theme.error)
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // KARAOKE BURN-IN (optional post-pass over a finished file)
    // ═══════════════════════════════════════════════════════════
    //
    // Deliberately not a single "Burn in" button. Burn-in is a second, slower
    // operation on a file that already exists, it writes a *new* file, and it
    // does not exist at all on a build without libavfilter. A lone button would
    // have to be silently dead on one build, would have nothing to say about the
    // file it is about to create, and would leave the user unable to tell a
    // finished pass from one that never started. So it is described before it is
    // offered: what it reads, what it will write, and why it cannot run right
    // now.
    //
    // The style is the panel's existing one throughout -- AppButton, PulseIndicator,
    // a caption in Theme.textSecondary -- so this reads as part of the Record tab
    // rather than as a new feature bolted onto it.

    SectionHeader {
        Layout.fillWidth: true
        text: "Karaoke burn-in"
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        AppButton {
            Layout.fillWidth: true
            text: RecordingBridge.burnInRunning ? "Burning in…" : "Burn in subtitles"
            enabled: !RecordingBridge.burnInRunning
                     && RecordingBridge.burnInBlockedReason.length === 0
            onClicked: RecordingBridge.burnInSubtitles()
            ToolTip.visible: hovered
            ToolTip.text: RecordingBridge.burnInRunning
                          ? "Re-encoding the whole file. This is not instant."
                          : root.burnInDescription
            ToolTip.delay: 300
        }

        // The panel's own word for "working": the Record/Stop row uses exactly
        // this indicator for a running capture, so a running pass is not a new
        // visual idea.
        PulseIndicator {
            active: RecordingBridge.burnInRunning
            baseColor: Theme.accent
            size: 12
        }
    }

    // Named before the click, so nothing is ever created by surprise and the
    // refusal to overwrite is a fact on screen rather than an error after the
    // fact.
    Text {
        Layout.fillWidth: true
        visible: root.burnInDescription.length > 0
        text: root.burnInDescription
        color: Theme.textSecondary
        font: Theme.fontCaption
        elide: Text.ElideMiddle
    }

    // One reason, not four booleans: an unsupported build, a missing sidecar and
    // a name collision are three different problems with three different fixes,
    // and a disabled button says none of them. Warning rather than error colour
    // because none of these is a failure -- the pass simply is not available yet.
    Text {
        Layout.fillWidth: true
        visible: RecordingBridge.burnInBlockedReason.length > 0
        text: RecordingBridge.burnInBlockedReason
        color: Theme.warning
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    // The sidecar is what burnInSubtitles() reads, so the panel is also where it
    // is written -- one name, one owner, rather than a user-chosen filename in a
    // different dialog that would then not match what the pass looks for. This is
    // the exportToAss entry point that had no QML caller at all.
    AppButton {
        Layout.fillWidth: true
        text: "Save karaoke subtitles (.ass)"
        enabled: RecordingBridge.burnInInputFile.length > 0
                 && LyricsBridge.hasLyrics
                 && !RecordingBridge.burnInRunning
        onClicked: LyricsBridge.exportToAss(RecordingBridge.burnInSubtitleFile)
        ToolTip.visible: hovered
        ToolTip.text: root.fileName(RecordingBridge.burnInSubtitleFile)
        ToolTip.delay: 300
    }

    Item { Layout.fillHeight: true }

    FileDialog {
        id: fileDialog
        title: "Save Recording"
        fileMode: FileDialog.SaveFile
        nameFilters: ["MP4 files (*.mp4)", "MKV files (*.mkv)"]
        onAccepted: outputPathField.text = toLocalFilePath(selectedFile)
    }
}
