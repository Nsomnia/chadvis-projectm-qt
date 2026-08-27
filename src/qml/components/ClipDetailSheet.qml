/**
 * @file ClipDetailSheet.qml
 * @brief Modal detail sheet for a Suno clip
 *
 * Rich browsing surface: large art, title/model/status/dates, prompt,
 * style tags and lyrics preview. Actions are honest about P2 scope:
 * remote streaming lands with playback parity, so "Open in browser"
 * delegates the audio URL to the OS until PlaylistBridge grows addUrl().
 *
 * Usage: set clipId(clip), call open(). Closes on Escape / outside click.
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis

Popup {
    id: root

    property var clipData: null

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
                        text: {
                            if (!root.clipData) return ""
                            const s = parseInt(root.clipData.duration) || 0
                            return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
                        }
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
            height: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Text {
                text: "Streaming lands with playback parity (P2)"
                color: Theme.textDisabled
                font: Theme.fontCaption
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            AppButton {
                text: "Open in Browser"
                icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
                enabled: !!root.clipData && !!root.clipData.audio_url
                onClicked: Qt.openUrlExternally(root.clipData.audio_url)
            }
        }
    }
}
