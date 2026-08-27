/**
 * @file ClipCard.qml
 * @brief Suno clip tile for the Library grid
 *
 * Large cover art with gradient scrim, status ribbon for in-progress
 * generations, model badge + duration + play-count meta row. Hover raises
 * the card and reveals the affordance button. Clicking emits opened(clip).
 *
 * Clip schema comes from SunoBridge.clips (see SunoBridge::onLibraryUpdated):
 *   id/title/status/image_url/model_name/duration(string secs)/play_count
 *   metadata{tags,prompt,lyrics}
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import ChadVis

Rectangle {
    id: root

    // QVariantMap for this clip (modelData from the bridge's QVariantList)
    property var clipData: null
    signal opened(var clip)

    readonly property bool isReady: clipData ? clipData.status === "complete" : false
    readonly property bool isHovered: cardMouse.containsMouse

    function formatSeconds(raw) {
        const s = parseInt(raw) || 0
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }

    function formatPlays(n) {
        if (!n || n <= 0)
            return ""
        if (n >= 1000)
            return (n / 1000).toFixed(1).replace(/\.0$/, "") + "k plays"
        return n + " plays"
    }

    width: Theme.cardTileMinimum
    height: Theme.cardTileMinimum * 1.22
    radius: Theme.radiusLarge
    color: Theme.surface

    border.width: root.isHovered ? 1 : 0
    border.color: Theme.glassBorder

    scale: root.isHovered ? 1.02 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
    Behavior on border.width { NumberAnimation { duration: Theme.durationInstant } }

    // ── Cover art ────────────────────────────────
    Rectangle {
        id: artClipper
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: parent.height - 74
        radius: Theme.radiusLarge
        color: Theme.backgroundAlt
        clip: true

        // Square bottom corners against the info block
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.height / 2
            color: artClipper.color
        }

        Image {
            anchors.fill: parent
            source: root.clipData ? (root.clipData.image_url || "") : ""
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        }

        // Placeholder note glyph while art loads (or when absent)
        Text {
            anchors.centerIn: parent
            visible: !root.clipData || !root.clipData.image_url
            text: "♪"
            color: Theme.textDisabled
            font.pixelSize: 42
        }

        // Scrim so the title zone melts into the card body
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.55; color: "transparent" }
                GradientStop { position: 1.0; color: Theme.withAlpha(Theme.background, 0.85) }
            }
        }
    }

    // ── Status ribbon (queued / generating) ─────
    Rectangle {
        visible: !root.isReady && !!root.clipData
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: Theme.spacingSmall
        implicitWidth: statusLabel.implicitWidth + 14
        implicitHeight: 20
        radius: Theme.radiusRound
        color: Theme.withAlpha(Theme.background, 0.75)
        border.width: 1
        border.color: Theme.warningDim

        Text {
            id: statusLabel
            anchors.centerIn: parent
            text: {
                if (!root.clipData) return ""
                const s = root.clipData.status
                return s === "queued" ? "Queued" : s === "running" ? "Creating…" : s
            }
            color: Theme.warning
            font: Theme.fontTiny
        }
    }

    // ── Hover affordance ────────────────────────
    Rectangle {
        anchors.centerIn: artClipper
        width: 52
        height: 52
        radius: Theme.radiusRound
        color: Theme.withAlpha(Theme.accent, 0.92)
        border.width: 2
        border.color: Theme.accentLight
        opacity: root.isHovered ? 1.0 : 0.0
        scale: root.isHovered ? 1.0 : 0.7
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutBack } }

        Text {
            anchors.centerIn: parent
            text: "▶"
            color: Theme.textOnAccent
            font.pixelSize: 18
        }
    }

    // ── Meta block ──────────────────────────────
    ColumnLayout {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingSmall
        spacing: 6

        Text {
            text: root.clipData ? (root.clipData.title || "Untitled") : ""
            color: Theme.textPrimary
            font: Theme.fontBodyStrong
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            Layout.fillWidth: true
            maximumLineCount: 1
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            // Model badge
            Rectangle {
                visible: !!root.clipData && !!root.clipData.model_name
                implicitWidth: modelLabel.implicitWidth + 12
                implicitHeight: 17
                radius: Theme.radiusRound
                color: Theme.glassHighlight
                border.width: 1
                border.color: Theme.glassBorder

                Text {
                    id: modelLabel
                    anchors.centerIn: parent
                    text: root.clipData ? (root.clipData.model_name || "") : ""
                    color: Theme.accentLight
                    font: Theme.fontTiny
                }
            }

            Text {
                text: root.clipData ? root.formatSeconds(root.clipData.duration) : ""
                color: Theme.textSecondary
                font: Theme.fontCaption
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.clipData ? root.formatPlays(root.clipData.play_count) : ""
                color: Theme.textDisabled
                font: Theme.fontCaption
            }
        }
    }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.opened(root.clipData)
    }
}

