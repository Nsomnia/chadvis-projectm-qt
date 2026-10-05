import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis

Rectangle {
    id: root

    // QVariantMap for this clip (modelData from the bridge's QVariantList)
    property var clipData: null
    signal opened(var clip)

    readonly property bool isReady: clipData ? clipData.status === "complete" : false
    readonly property bool canPlay: isReady && clipData && clipData.has_media !== false
    readonly property bool canSave: canPlay
    readonly property bool isHovered: cardMouse.containsMouse

    // ── Save / selection state ────────────────────────
    // Supplied by the view rather than kept here. A GridView recycles its
    // delegates, so a card that remembered "already on disk" in its own
    // properties would forget it the moment the tile scrolled off and back.
    // The owning view holds the facts; the card renders them.
    //
    // saveState is "idle" | "saving" | "saved" | "refused". It is never an
    // empty string: "idle" is a real state, and the refusal text lives in
    // saveErrorText so the button can offer a retry without inventing a
    // reason of its own.
    property string saveState: "idle"
    property string savedPath: ""
    property string saveErrorText: ""

    property bool selectionMode: false
    property bool selected: false

    signal saveRequested(string clipId)
    signal selectionToggled(var clip)
    /// Global (window) coordinates, so the view can popup a Menu without
    /// needing a handle on this delegate.
    signal contextRequested(var clip, point globalPos)

    function fileName(path) {
        if (!path)
            return ""
        const parts = String(path).split(/[\\/]/)
        return parts.length > 0 ? parts[parts.length - 1] : path
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

        // Save / selection result, and only when there is something to say.
        // A refused save is the case that matters: the refusal already arrives
        // as a sentence written by SunoDownloader (noUsableMediaMessage), so it
        // is shown as-is rather than replaced with a generic failure.
        //
        // Sized to one line and elided because a refusal sentence is long and
        // the card's height is fixed by the grid cell. The full sentence is on
        // the button's tooltip and in the Library footer.
        Text {
            Layout.fillWidth: true
            visible: root.saveState !== "idle"
            text: {
                if (root.saveState === "saving")
                    return "Saving " + (root.clipData ? (root.clipData.title || "clip") : "clip")
                if (root.saveState === "saved")
                    return "Saved " + root.fileName(root.savedPath)
                if (root.saveState === "refused")
                    return root.saveErrorText
                return ""
            }
            color: root.saveState === "refused" ? Theme.error
                    : (root.saveState === "saved" ? Theme.success : Theme.textSecondary)
            font: Theme.fontTiny
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
        }
    }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                root.contextRequested(root.clipData, root.mapToGlobal(mouse.x, mouse.y))
                return
            }
            if (root.selectionMode) {
                root.selectionToggled(root.clipData)
                return
            }
            const dx = mouse.x - root.width / 2
            const dy = mouse.y - artClipper.height / 2
            if (root.canPlay && dx * dx + dy * dy <= 26 * 26) {
                SunoBridge.playClip(root.clipData.id)
                return
            }
            root.opened(root.clipData)
        }
    }

    // ── Selection marker ─────────────────────────
    // Occupies the save button's corner (top-right) and only exists while
    // selecting, so the two never overlap and a card in selection mode has one
    // obvious thing its corner does. Declared after the card-wide MouseArea, so
    // it takes the click rather than needing a hit-test rectangle.
    Rectangle {
        visible: root.selectionMode
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacingSmall
        width: 22
        height: 22
        radius: Theme.radiusRound
        color: root.selected ? Theme.accent : Theme.withAlpha(Theme.background, 0.75)
        border.width: 1
        border.color: root.selected ? Theme.accentLight : Theme.glassBorder

        Text {
            anchors.centerIn: parent
            visible: root.selected
            text: "✓"
            color: Theme.textOnAccent
            font: Theme.fontTiny
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.selectionToggled(root.clipData)
        }

        Accessible.role: Accessible.CheckBox
        Accessible.checked: root.selected
        Accessible.name: root.clipData ? (root.clipData.title || "Untitled") : "Clip"
    }

    // ── Save button ──────────────────────────────
    // Top-right of the cover. AppButton rather than a bespoke rectangle: the
    // press animation, focus ring and Accessible wiring are already there, and
    // a hand-rolled one would be a fourth copy of that behaviour.
    AppButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacingSmall
        implicitWidth: 28
        implicitHeight: 28
        buttonRadius: Theme.radiusSmall
        // Not flat: over cover art the button needs its own surface to read as
        // a control, which is exactly what AppButton's non-flat treatment is.
        flat: false
        // Hidden while selecting: in that mode a click means "add to the
        // selection", and a corner button that quietly does something else is
        // the ambiguity this mode exists to remove.
        visible: root.canSave && !root.selectionMode
        text: {
            if (root.saveState === "saving") return "…"
            if (root.saveState === "saved") return "✓"
            if (root.saveState === "refused") return "↻"
            return "↓"
        }
        // Deliberately NOT `highlighted` on refusal, unlike the detail sheet's
        // retry button: AppButton's highlight runs a looping PropertyAnimation,
        // and a batch save can mark a dozen cards refused at once. The glyph
        // and the caption's red already say it failed.
        //
        // AppButton derives Accessible.name from `text`, which here is a glyph.
        Accessible.name: {
            const title = root.clipData ? (root.clipData.title || "this clip") : "this clip"
            if (root.saveState === "saving")
                return "Saving " + title
            if (root.saveState === "saved")
                return title + " is saved. Save again"
            if (root.saveState === "refused")
                return "Could not save " + title + ". Try again"
            return "Save " + title + " to your downloads folder"
        }
        ToolTip.visible: hovered
        ToolTip.delay: 400
        ToolTip.text: {
            if (root.saveState === "refused")
                return root.saveErrorText
            if (root.saveState === "saved")
                return "Saved " + root.savedPath
            const dir = String(SettingsBridge.sunoDownloadPath)
            return dir.length > 0 ? "Save to " + dir : "Save to your music folder"
        }
        onClicked: root.saveRequested(root.clipData ? root.clipData.id : "")
    }
}

