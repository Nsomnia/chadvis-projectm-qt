import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import ChadVis
import "../components"
import "../panels"

Item {
    id: root

    function toLocalFilePath(url) {
        const value = String(url)
        if (!value.startsWith("file://"))
            return value
        let path = decodeURIComponent(value.slice("file://".length))
        if (/^\/[A-Za-z]:\//.test(path))
            path = path.slice(1)
        return path
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingMedium

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            ColumnLayout {
                spacing: Theme.spacingTiny

                Text {
                    text: "Create"
                    color: Theme.textPrimary
                    font: Theme.fontHeading
                }

                Text {
                    text: "Use the models available to your Suno session. Generation stays disabled until the required CAPTCHA token flow is supported."
                    color: Theme.textSecondary
                    font: Theme.fontBody
                }
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                AppButton {
                    text: SunoBridge.audioUploadBusy ? "Uploading…" : "Upload audio"
                    enabled: SunoBridge.isAuthenticated && !SunoBridge.audioUploadBusy
                    onClicked: audioFileDialog.open()
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: SunoBridge.audioUploadBusy
                    text: SunoBridge.audioUploadProgress + "%"
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 5
                visible: SunoBridge.audioUploadBusy
                radius: 2
                color: Theme.surfaceRaised

                Rectangle {
                    width: parent.width * SunoBridge.audioUploadProgress / 100
                    height: parent.height
                    radius: parent.radius
                    color: Theme.accent
                }
            }

            Text {
                Layout.fillWidth: true
                text: SunoBridge.audioUploadStatus
                color: Theme.textSecondary
                font: Theme.fontCaption
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: SunoBridge.audioUploadError.length > 0
                text: SunoBridge.audioUploadError
                color: Theme.error
                font: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
        }

        SunoPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    FileDialog {
        id: audioFileDialog
        title: "Upload Audio to Suno"
        fileMode: FileDialog.OpenFile
        nameFilters: ["M4A audio (*.m4a)"]
        onAccepted: {
            if (selectedFile)
                SunoBridge.uploadAudio(toLocalFilePath(selectedFile))
        }
    }
}
