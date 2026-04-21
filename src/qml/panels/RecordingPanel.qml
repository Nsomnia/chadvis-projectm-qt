import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import ChadVis
import "../components"

ColumnLayout {
    id: root

    spacing: Theme.spacingMedium

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
            highlighted: !RecordingBridge.isRecording
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
        Rectangle {
            width: 12
            height: 12
            radius: 6
            color: RecordingBridge.isRecording ? Theme.recording : Theme.textDisabled

            SequentialAnimation on opacity {
                running: RecordingBridge.isRecording
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 500 }
                NumberAnimation { to: 1.0; duration: 500 }
            }
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

        ComboBox {
            id: codecCombo
            Layout.fillWidth: true
            model: ["libx264", "libx265", "libvpx-vp9", "h264_nvenc", "hevc_nvenc"]
            currentIndex: 0
            
            contentItem: Text {
                text: codecCombo.displayText
                font: Theme.fontBody
                color: Theme.textPrimary
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.spacingSmall
            }

            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceRaised
                border.color: Theme.border
            }
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
                value: 23
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

            TextField {
                id: outputPathField
                Layout.fillWidth: true
                placeholderText: "Auto-generated if empty"
                color: Theme.textPrimary
                font: Theme.fontBody
                
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    border.color: Theme.border
                }
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
                Text { text: RecordingBridge.framesWritten; color: Theme.textPrimary; font: Theme.fontCaption; font.bold: true }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Size"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
                Text { text: RecordingBridge.fileSize; color: Theme.textPrimary; font: Theme.fontCaption; font.bold: true }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "FPS"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
                Text { text: RecordingBridge.encodeFps; color: Theme.textPrimary; font: Theme.fontCaption; font.bold: true }
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

    Item { Layout.fillHeight: true }

    FileDialog {
        id: fileDialog
        title: "Save Recording"
        fileMode: FileDialog.SaveFile
        nameFilters: ["MP4 files (*.mp4)", "MKV files (*.mkv)"]
        onAccepted: outputPathField.text = selectedFile.toString().replace("file://", "")
    }
}
