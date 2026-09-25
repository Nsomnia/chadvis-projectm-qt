import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis

Popup {
    id: root

    property var clipData: null

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

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Text {
                text: SunoBridge.errorMessage.length > 0
                      ? SunoBridge.errorMessage
                      : (SunoBridge.downloadStatus.length > 0
                         ? SunoBridge.downloadStatus
                         : "ChadVis selects captured clip media and plays it through the local audio engine.")
                visible: text.length > 0
                color: SunoBridge.errorMessage.length > 0
                       ? Theme.error : Theme.textDisabled
                font: Theme.fontCaption
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            AppButton {
                text: "Download & Play"
                enabled: !!root.clipData
                         && root.clipData.status === "complete"
                         && root.clipData.has_media !== false
                onClicked: SunoBridge.playClip(root.clipData.id)
            }
        }
    }
}
