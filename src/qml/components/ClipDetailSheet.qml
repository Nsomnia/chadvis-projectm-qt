import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis

Popup {
    id: root

    property var clipData: null

    // Save outcome for the clip on show, owned by the view that opened the
    // sheet (LibraryView). Held here rather than read from the bridge because
    // downloadStatus is one string describing the most recent event across all
    // clips, so it cannot answer "did THIS one land".
    property string saveState: "idle"
    property string savedPath: ""
    property string saveErrorText: ""

    signal saveRequested(string clipId)

    readonly property bool canSave: !!clipData
                                   && clipData.status === "complete"
                                   && clipData.has_media !== false

    function fileName(path) {
        if (!path)
            return ""
        const parts = String(path).split(/[\\/]/)
        return parts.length > 0 ? parts[parts.length - 1] : path
    }

    // QML's url type has no toLocalFile(); build the URL by hand the same way
    // PlaylistPanel does. Opening the file (not its directory) is what makes a
    // file manager select it.
    function localFileUrl(path) {
        const p = String(path).replace(/\\/g, "/")
        return p.startsWith("/") ? "file://" + encodeURI(p)
                                 : "file:///" + encodeURI(p)
    }

    function formatDuration(raw) {
        if (raw === undefined || raw === null || raw === "")
            return "0:00"

        const parts = String(raw).split(":")
        let seconds = 0
        if (parts.length === 2)
            seconds = Number(parts[0]) * 60 + Number(parts[1])
        else if (parts.length >= 3)
            seconds = Number(parts[parts.length - 3]) * 3600
                    + Number(parts[parts.length - 2]) * 60
                    + Number(parts[parts.length - 1])
        else
            seconds = Number(raw)

        if (!Number.isFinite(seconds) || seconds < 0)
            return "0:00"
        const whole = Math.floor(seconds)
        return Math.floor(whole / 60) + ":" + (whole % 60 < 10 ? "0" : "") + (whole % 60)
    }

    width: Math.min(560, parent ? parent.width - Theme.spacingXL * 2 : 560)
    height: Math.min(620, parent ? parent.height - Theme.spacingXL * 2 : 620)
    anchors.centerIn: Overlay.overlay
    modal: true
    padding: 0

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.glassBorder
    }

    contentItem: ColumnLayout {
        spacing: 0

        // ── Banner ──────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 190

            Image {
                anchors.fill: parent
                source: root.clipData ? (root.clipData.image_url || "") : ""
                fillMode: Image.PreserveAspectCrop
                visible: status === Image.Ready
            }

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Theme.withAlpha(Theme.surfaceRaised, 0.98) }
                }
            }

            // Close button
            AppButton {
                icon: "qrc:/qt/qml/ChadVis/resources/icons/clear.svg"
                flat: true
                implicitWidth: 32
                implicitHeight: 32
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spacingSmall
                onClicked: root.close()
            }

            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingTiny

                Text {
                    text: root.clipData ? (root.clipData.title || "Untitled") : ""
                    color: Theme.textPrimary
                    font: Theme.fontTitle
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: Theme.spacingSmall

                    Rectangle {
                        visible: !!root.clipData && !!root.clipData.model_name
                        implicitWidth: modelNameLabel.implicitWidth + 12
                        implicitHeight: 18
                        radius: Theme.radiusRound
                        color: Theme.glassHighlight
                        border.width: 1
                        border.color: Theme.glassBorder

                        Text {
                            id: modelNameLabel
                            anchors.centerIn: parent
                            text: root.clipData ? (root.clipData.model_name || "") : ""
                            color: Theme.accentLight
                            font: Theme.fontTiny
                        }
                    }

                    Text {
                        text: root.clipData ? root.formatDuration(root.clipData.duration) : ""
                        color: Theme.textSecondary
                        font: Theme.fontCaption
                    }

                    Text {
                        visible: !!root.clipData && root.clipData.play_count > 0
                        text: root.clipData ? root.clipData.play_count + " plays" : ""
                        color: Theme.textSecondary
                        font: Theme.fontCaption
                    }

                    Text {
                        visible: !!root.clipData && !!root.clipData.created_at
                        text: root.clipData ? "· " + String(root.clipData.created_at).split("T")[0] : ""
                        color: Theme.textDisabled
                        font: Theme.fontCaption
                    }
                }
            }
        }

        // ── Body ────────────────────────────────────
        Flickable {
            id: bodyFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: bodyLayout.implicitHeight + Theme.spacingLarge * 2
            clip: true

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: bodyLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingMedium

                // Style tags
                Flow {
                    visible: !!root.clipData && !!(root.clipData.metadata ? root.clipData.metadata.tags : "")
                    spacing: Theme.spacingTiny
                    Layout.fillWidth: true

                    Repeater {
                        model: {
                            if (!root.clipData) return []
                            const tags = root.clipData.metadata ? root.clipData.metadata.tags : ""
                            return tags ? tags.split(/[ ,]+/).filter(t => t.length > 0) : []
                        }

                        delegate: Rectangle {
                            implicitWidth: tagText.implicitWidth + 14
                            implicitHeight: 20
                            radius: Theme.radiusRound
                            color: Theme.surfaceOverlay

                            Text {
                                id: tagText
                                anchors.centerIn: parent
                                text: modelData
                                color: Theme.textPrimaryVariant
                                font: Theme.fontTiny
                            }
                        }
                    }
                }

                // Destination, stated again beside the body rather than only in the footer,
                // because the footer's line is elided and a path is the one
                // string here that must not be guessed at.
                Text {
                    Layout.fillWidth: true
                    visible: root.saveState === "saved" && root.savedPath.length > 0
                    text: root.savedPath
                    color: Theme.textDisabled
                    font: Theme.fontTiny
                    elide: Text.ElideMiddle
                }

                // Prompt
                ColumnLayout {
                    visible: !!root.clipData && !!(root.clipData.metadata ? root.clipData.metadata.prompt : "")
                    spacing: Theme.spacingTiny
                    Layout.fillWidth: true

                    Text {
                        text: "PROMPT"
                        color: Theme.textDisabled
                        font: Theme.fontCaptionStrong
                    }

                    Text {
                        text: root.clipData && root.clipData.metadata ? root.clipData.metadata.prompt : ""
                        color: Theme.textPrimaryVariant
                        font: Theme.fontBody
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                // Lyrics preview
                ColumnLayout {
                    visible: {
                        if (!root.clipData || !root.clipData.metadata) return false
                        const l = root.clipData.metadata.lyrics
                        return !!l && l !== "[Instrumental]"
                    }
                    spacing: Theme.spacingTiny
                    Layout.fillWidth: true

                    Text {
                        text: "LYRICS"
                        color: Theme.textDisabled
                        font: Theme.fontCaptionStrong
                    }

                    Text {
                        text: {
                            if (!root.clipData || !root.clipData.metadata) return ""
                            const lines = String(root.clipData.metadata.lyrics).split("\n")
                            const head = lines.slice(0, 12).join("\n")
                            return lines.length > 12 ? head + "\n…" : head
                        }
                        color: Theme.textSecondary
                        font: Theme.fontBody
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Text {
                    visible: !!root.clipData && root.clipData.metadata
                           && root.clipData.metadata.lyrics === "[Instrumental]"
                    text: "♪ Instrumental"
                    color: Theme.textSecondary
                    font: Theme.fontBody
                }
            }
        }

        // ── Actions ─────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMedium
            Layout.bottomMargin: Theme.spacingSmall
            spacing: Theme.spacingTiny

            // Where a save lands, before the user commits to one. A save button
            // whose destination is invisible is a save button nobody trusts.
            // Empty sunoDownloadPath means SunoDownloader::getDownloadDir falls
            // back to the system music folder, so the copy says that rather than
            // showing an empty path.
            Text {
                Layout.fillWidth: true
                visible: root.canSave
                text: {
                    const dir = String(SettingsBridge.sunoDownloadPath)
                    return dir.length > 0 ? "Saves to " + dir
                                          : "Saves to your music folder"
                }
                color: Theme.textDisabled
                font: Theme.fontTiny
                elide: Text.ElideMiddle
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                // The save result for THIS clip. Two lines' worth of honesty:
                // the actual filename on success, and the downloader's own
                // refusal sentence on failure, rather than a generic error.
                Text {
                    Layout.fillWidth: true
                    text: {
                        if (root.saveState === "saving")
                            return "Saving " + (root.clipData ? (root.clipData.title || "clip") : "clip")
                        if (root.saveState === "saved")
                            return "Saved " + root.fileName(root.savedPath)
                        if (root.saveState === "refused")
                            return root.saveErrorText
                        if (SunoBridge.errorMessage.length > 0)
                            return SunoBridge.errorMessage
                        if (SunoBridge.downloadStatus.length > 0)
                            return SunoBridge.downloadStatus
                        return ""
                    }
                    visible: text.length > 0
                    color: root.saveState === "refused"
                           || (root.saveState === "idle" && SunoBridge.errorMessage.length > 0)
                           ? Theme.error
                           : (root.saveState === "saved" ? Theme.success : Theme.textSecondary)
                    font: Theme.fontCaption
                    // A refusal is a sentence the downloader wrote on purpose,
                    // explaining exactly which media rule refused the clip. One
                    // elided line throws that away, so a refusal is allowed
                    // three; every other state is a filename and stays on one.
                    wrapMode: Text.WordWrap
                    maximumLineCount: root.saveState === "refused" ? 3 : 1
                    elide: Text.ElideRight
                    Layout.maximumHeight: Theme.fontCaption.pixelSize * 3.4
                }

                AppButton {
                    visible: root.saveState === "saved"
                    text: "Show file"
                    flat: true
                    implicitHeight: Theme.buttonHeight
                    onClicked: Qt.openUrlExternally(root.localFileUrl(root.savedPath))
                    ToolTip.visible: hovered
                    ToolTip.text: root.savedPath
                    ToolTip.delay: 300
                    Accessible.name: "Show " + root.savedPath + " in the file manager"
                }

                // Play and save are two verbs and now two buttons. The old
                // single "Download & Play" button promised a download and only
                // ever played, which is the kind of label that teaches a user
                // the UI is lying.
                AppButton {
                    text: "Play"
                    flat: true
                    enabled: !!root.clipData
                             && root.clipData.status === "complete"
                             && root.clipData.has_media !== false
                    onClicked: SunoBridge.playClip(root.clipData.id)
                    Accessible.name: "Play " + (root.clipData ? (root.clipData.title || "this clip") : "this clip")
                }

                AppButton {
                    text: {
                        if (root.saveState === "saving") return "Saving…"
                        if (root.saveState === "saved") return "Save again"
                        if (root.saveState === "refused") return "Retry save"
                        return "Save"
                    }
                    enabled: root.canSave && root.saveState !== "saving"
                    highlighted: root.saveState === "refused"
                    onClicked: root.saveRequested(root.clipData.id)
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: {
                        if (root.saveState === "refused")
                            return root.saveErrorText
                        if (root.saveState === "saved")
                            return "Already at " + root.savedPath
                        return "Save a copy to your downloads folder"
                    }
                    Accessible.name: "Save " + (root.clipData ? (root.clipData.title || "this clip") : "this clip")
                }
            }
        }
    }
}
