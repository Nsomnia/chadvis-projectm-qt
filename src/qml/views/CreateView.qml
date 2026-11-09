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
                    text: "Write a prompt, pick a model, and Suno renders it here. "
                          + "The song lands in your library as it finishes."
                    color: Theme.textSecondary
                    font: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        // ── Generate ──────────────────────────────────────────────
        // Prompt is the only required field. The captcha notice is not an error
        // banner: this account's preflight answers `required: true`, so it is the
        // expected state and it names where the token comes from.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            AppTextField {
                id: promptField
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                placeholderText: "Describe the song, or paste lyrics with [Verse] / [Chorus] tags"
                Accessible.name: "Song prompt or lyrics"
                wrapMode: TextEdit.Wrap
                font: Theme.fontBody
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                AppTextField {
                    id: tagsField
                    Layout.fillWidth: true
                    placeholderText: "Tags — pop, warm synths, 120 bpm"
                    Accessible.name: "Style tags"
                    font: Theme.fontBody
                }

                AppTextField {
                    id: modelField
                    Layout.preferredWidth: 180
                    text: "chirp-v3-5"
                    placeholderText: "model"
                    Accessible.name: "Model name"
                    font: Theme.fontBody
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Text {
                    text: "Instrumental"
                    color: Theme.textSecondary
                    font: Theme.fontBody
                }
                AppSwitch {
                    id: instrumentalSwitch
                    checked: false
                    Accessible.name: "Instrumental"
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: SunoBridge.generationState === "rendering"
                    text: "Rendering…"
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                }

                AppButton {
                    text: (SunoBridge.generationState === "submitting"
                           || SunoBridge.generationState === "preflight")
                          ? "Submitting…" : "Generate"
                    highlighted: true
                    enabled: SunoBridge.isAuthenticated && promptField.text.length > 0
                             && SunoBridge.generationState !== "submitting"
                             && SunoBridge.generationState !== "rendering"
                    onClicked: SunoBridge.submitGeneration(promptField.text, tagsField.text, "",
                                                           instrumentalSwitch.checked,
                                                           modelField.text, "")
                }
            }

            Text {
                Layout.fillWidth: true
                visible: SunoBridge.generationCaptchaRequired
                text: "Suno wants a captcha before it accepts this song. Open suno.com/create, "
                      + "start one generation there, then press Generate again here."
                color: Theme.warning
                font: Theme.fontCaption
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: SunoBridge.generationError.length > 0
                text: SunoBridge.generationError
                color: Theme.warning
                font: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
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

    onVisibleChanged: {
        if (!visible)
            createToast.clear()
    }

    Connections {
        target: SunoBridge
        ignoreUnknownSignals: true

        // Generation ready (plan §6.3): fixed sentence + an action
        // that opens the Library. Errors reuse the bridge's strings
        // verbatim — the same sentences the inline lines show.
        function onGenerationClipsReady(clips) {
            createToast.show("Song ready — open in Library", false, "Open Library")
        }

        function onGenerationErrorChanged() {
            if (SunoBridge.generationError.length > 0)
                createToast.show(SunoBridge.generationError, true)
        }

        function onAudioUploadErrorChanged() {
            if (SunoBridge.audioUploadError.length > 0)
                createToast.show(SunoBridge.audioUploadError, true)
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

    // Same overlay contract as the library toast: bottom-centre, never
    // pushes layout, clears when the route changes.
    Toast {
        id: createToast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLarge
        z: 10

        onActionClicked: {
            if (typeof mainWindow !== "undefined" && mainWindow.navigate)
                mainWindow.navigate("library")
        }
    }
}
