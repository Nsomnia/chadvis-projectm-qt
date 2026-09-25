import QtQuick
import QtQuick.Layouts
import ChadVis

Rectangle {
    id: root

    // QVariantMap for this clip (modelData from the bridge's QVariantList)
    property var clipData: null
    signal opened(var clip)

    readonly property bool isReady: clipData ? clipData.status === "complete" : false
    readonly property bool canPlay: isReady && clipData && clipData.has_media !== false
    readonly property bool isHovered: cardMouse.containsMouse

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
        visible: root.canPlay
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
                text: root.clipData ? root.formatDuration(root.clipData.duration) : ""
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
        onClicked: function(mouse) {
            const dx = mouse.x - root.width / 2
            const dy = mouse.y - artClipper.height / 2
            if (root.canPlay && dx * dx + dy * dy <= 26 * 26) {
                SunoBridge.playClip(root.clipData.id)
                return
            }
            root.opened(root.clipData)
        }
    }
}

