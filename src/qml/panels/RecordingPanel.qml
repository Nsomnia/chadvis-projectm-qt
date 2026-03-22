import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import ChadVis
import "../components"

ColumnLayout {
    id: root

    property bool isRecording: RecordingBridge ? RecordingBridge.isRecording : false
    property string selectedCodec: "libx264"
    property int selectedQuality: 23
    property string outputPath: ""

    spacing: 0

    ToolBar {
        Layout.fillWidth: true
        background: Rectangle { color: Theme.surfaceVariant }

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Label {
                text: "Video Recording"
                color: Theme.onSurface
                font.pixelSize: Theme.fontSizeMedium
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 12
                height: 12
                radius: 6
                color: root.isRecording ? Theme.error : Theme.onSurfaceVariant

                SequentialAnimation on opacity {
                    running: root.isRecording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }
        }
    }

    Pane {
        Layout.fillWidth: true
        background: Rectangle { color: Theme.surface }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingMedium

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

AppButton {
            id: recordButton
            text: root.isRecording ? "\u25A0 Stop" : "\u25B6 Record"
            highlighted: !root.isRecording
            onClicked: {
                        if (root.isRecording) {
                            RecordingBridge.stopRecording()
                        } else {
                            RecordingBridge.startRecording(root.outputPath)
                        }
                    }
                }

                Label {
                    visible: root.isRecording
                    text: RecordingBridge ? RecordingBridge.recordingTime : "00:00"
                    color: Theme.error
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spacingMedium
                rowSpacing: Theme.spacingSmall

                Label {
                    text: "Codec:"
                    color: Theme.onSurfaceVariant
                }

                ComboBox {
                    id: codecCombo
                    Layout.fillWidth: true
                    model: ["libx264", "libx265", "libvpx-vp9", "h264_nvenc", "hevc_nvenc"]
                    currentIndex: model.indexOf(root.selectedCodec)
                    onActivated: root.selectedCodec = model[index]

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surfaceVariant
                        border.color: Theme.outline
                    }
                }

                Label {
                    text: "Quality (CRF):"
                    color: Theme.onSurfaceVariant
                }

                RowLayout {
                    Layout.fillWidth: true
                    Slider {
                        id: qualitySlider
                        Layout.fillWidth: true
                        from: 0
                        to: 51
                        stepSize: 1
                        value: root.selectedQuality
                        onValueChanged: root.selectedQuality = value
                    }
                    Label {
                        text: Math.round(qualitySlider.value)
                        color: Theme.onSurface
                        font.pixelSize: Theme.fontSizeMedium
                        Layout.preferredWidth: 30
                    }
                }

                Label {
                    text: "Output:"
                    color: Theme.onSurfaceVariant
                }

                TextField {
                    id: outputPathField
                    Layout.fillWidth: true
                    text: root.outputPath
                    placeholderText: "/path/to/output.mp4"
                    onTextChanged: root.outputPath = text

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surfaceVariant
                        border.color: Theme.outline
                    }
                }
            }

            AppButton {
                text: "Browse..."
                onClicked: fileDialog.open()
            }
        }
    }

    Pane {
        Layout.fillWidth: true
        visible: root.isRecording
        background: Rectangle { color: Theme.surfaceVariant }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Label {
                text: "Recording Stats"
                font.bold: true
                color: Theme.onSurface
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spacingMedium
                rowSpacing: Theme.spacingSmall

                Label {
                    text: "Frames Written:"
                    color: Theme.onSurfaceVariant
                }
                Label {
                    text: RecordingBridge ? RecordingBridge.framesWritten : "0"
                    color: Theme.onSurface
                }

                Label {
                    text: "Frames Dropped:"
                    color: Theme.onSurfaceVariant
                }
                Label {
                    text: RecordingBridge ? RecordingBridge.framesDropped : "0"
                    color: Theme.error
                }

                Label {
                    text: "File Size:"
                    color: Theme.onSurfaceVariant
                }
                Label {
                    text: RecordingBridge ? RecordingBridge.fileSize : "0 MB"
                    color: Theme.onSurface
                }

                Label {
                    text: "Encode FPS:"
                    color: Theme.onSurfaceVariant
                }
                Label {
                    text: RecordingBridge ? RecordingBridge.encodeFps : "0"
                    color: Theme.onSurface
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    text: "Buffer:"
                    color: Theme.onSurfaceVariant
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 8
                    radius: 4
                    color: Theme.surface

                    Rectangle {
                        width: parent.width * (RecordingBridge ? RecordingBridge.bufferHealth / 100 : 1)
                        height: parent.height
                        radius: parent.radius
                        color: {
                            var health = RecordingBridge ? RecordingBridge.bufferHealth : 100
                            if (health > 70) return Theme.success
                            if (health > 30) return Theme.warning
                            return Theme.error
                        }
                    }
                }
            }
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.margins: Theme.spacingMedium
        visible: !root.isRecording
        text: "Click Record to start capturing.\nOutput will be saved to the specified file."
        color: Theme.onSurfaceVariant
        font.pixelSize: Theme.fontSizeSmall
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    FileDialog {
        id: fileDialog
        title: "Save Recording"
        nameFilters: ["MP4 files (*.mp4)", "MKV files (*.mkv)", "WebM files (*.webm)"]
        onAccepted: root.outputPath = selectedFile
    }
}
