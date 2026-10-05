import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

Item {
    id: root

    // Query lives here (not on the field) so sibling bindings that appear
    // earlier in the document never reference a not-yet-created id.
    property string query: ""

    // ── Save state, owned here ──────────────────────────
    // clipId -> { state, path, reason }. A JS object rather than per-delegate
    // properties because GridView recycles delegates: a card that owned its own
    // "saved" state would forget it on scroll and claim a saved track was never
    // saved. Reassigned wholesale (never mutated in place) so the bindings on
    // visible delegates actually re-evaluate; the map holds only what this
    // session saved or refused, so the copy is small.
    property var saveStates: ({})
    property string lastSavedPath: ""

    // ── Multi-select ────────────────────────────────────
    // Array of clip ids. Small by construction (only what the user picked), so
    // indexOf is not worth replacing with a set.
    property var selectedIds: []
    property bool selectionMode: false

    readonly property int selectedCount: selectedIds.length

    function saveStateFor(clipId) {
        const entry = saveStates[clipId]
        return entry ? entry.state : "idle"
    }

    function savedPathFor(clipId) {
        const entry = saveStates[clipId]
        return entry && entry.path ? entry.path : ""
    }

    function saveErrorFor(clipId) {
        const entry = saveStates[clipId]
        return entry && entry.reason ? entry.reason : ""
    }

    function setSaveState(clipId, state, path, reason) {
        if (!clipId)
            return
        const updated = Object.assign({}, saveStates)
        updated[clipId] = { state: state, path: path || "", reason: reason || "" }
        saveStates = updated
        if (state === "saved")
            lastSavedPath = path || ""
    }

    // Bulk form of setSaveState, and the only one a batch uses: one map copy and
    // one notify for the whole request, instead of N. A 200-clip batch marking
    // each clip "saving" one at a time would copy the map 200 times and
    // re-evaluate every visible delegate's bindings 200 times for a state that
    // is identical for all of them at that instant.
    function markSaving(clipIds) {
        const updated = Object.assign({}, saveStates)
        for (let i = 0; i < clipIds.length; ++i) {
            const clipId = clipIds[i]
            if (!clipId)
                continue
            const existing = updated[clipId]
            updated[clipId] = {
                state: "saving",
                path: existing && existing.path ? existing.path : "",
                reason: ""
            }
        }
        saveStates = updated
    }

    // A refusal is decided inside SunoBridge::downloadInto before the call
    // returns, and it arrives as a sentence on errorMessage -- so reading
    // errorMessage straight after a SINGLE-clip request attributes the reason to
    // that clip exactly, with no timer and no guessing.
    //
    // For a batch the same string is a summary ("N clips could not be saved.
    // First reason: …") covering an unknown subset, so it is NOT painted onto
    // every card: that would mark clips which are in fact transferring as
    // refused. The batch reason goes to the Library footer, which already
    // prefers errorMessage, and per-card outcomes arrive on clipSaved(). A
    // card left "Saving" after its transfer failed is cleared by the
    // downloadStatusChanged handler below; a per-clip REJECTION inside a batch
    // needs a C++ signal that does not exist yet (see the report).
    function saveClips(clipIds, singleId) {
        if (!clipIds || clipIds.length === 0)
            return
        markSaving(clipIds)

        if (singleId !== undefined) {
            SunoBridge.downloadClip(singleId)
            const reason = String(SunoBridge.errorMessage)
            if (reason.length > 0)
                setSaveState(singleId, "refused", "", reason)
        } else {
            SunoBridge.downloadClips(clipIds)
        }
    }

    function toggleSelection(clip) {
        if (!clip || !clip.id)
            return
        const next = selectedIds.slice()
        const at = next.indexOf(clip.id)
        if (at >= 0)
            next.splice(at, 1)
        else
            next.push(clip.id)
        selectedIds = next
    }

    function isSelected(clipId) {
        return selectedIds.indexOf(clipId) >= 0
    }

    function clearSelection() {
        selectedIds = []
        selectionMode = false
    }

    // QML's url value type has no toLocalFile(), so the reverse is built by
    // hand: normalize separators and percent-encode. Naive "file://" + path
    // concatenation corrupts spaces and '#'/'?', and a Windows drive path needs
    // the extra slash. Same shape as PlaylistPanel's localFileUrl().
    function localFileUrl(path) {
        const p = String(path).replace(/\\/g, "/")
        return p.startsWith("/") ? "file://" + encodeURI(p)
                                 : "file:///" + encodeURI(p)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ═════════════════════════════════════════════
        // HEADER
        // ═════════════════════════════════════════════
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            Layout.bottomMargin: Theme.spacingMedium
            spacing: Theme.spacingMedium

            ColumnLayout {
                spacing: 2

                Text {
                    text: "Library"
                    color: Theme.textPrimary
                    font: Theme.fontDisplay
                }

                Text {
                    text: {
                        const n = SunoBridge.clips.length
                        const scope = root.query.length > 0 ? "matching “" + root.query + "”" : "in your collection"
                        return n > 0 ? n + " track" + (n === 1 ? "" : "s") + " " + scope : "Your Suno collection"
                    }
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                }
            }

            Item { Layout.fillWidth: true }

            AppTextField {
                id: searchField
                Layout.preferredWidth: 280
                placeholderText: "Search titles & styles…"
                color: Theme.textPrimary
                font: Theme.fontBody
                text: root.query

                onTextChanged: {
                    root.query = text
                    SunoBridge.searchLibrary(text)
                }

                Image {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    source: searchField.text.length > 0 ? "qrc:/qt/qml/ChadVis/resources/icons/clear.svg"
                                                        : ""
                    visible: source !== ""
                    sourceSize: Qt.size(16, 16)
                    width: 16
                    height: 16
                    fillMode: Image.PreserveAspectFit
                    opacity: clearMouse.containsMouse ? 1.0 : 0.6

                    MouseArea {
                        id: clearMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: searchField.text = ""
                    }
                }
            }

            AppButton {
                // Selection mode exists for the batch save, so it turns itself
                // off when nothing is selected: a mode with no selection is
                // just a card whose click stopped working.
                text: root.selectionMode ? "Cancel select" : "Select"
                flat: true
                highlighted: root.selectionMode
                onClicked: {
                    if (root.selectionMode)
                        root.clearSelection()
                    else
                        root.selectionMode = true
                }
                ToolTip.visible: hovered
                ToolTip.text: root.selectionMode
                              ? "Leave selection mode"
                              : "Select several clips to save at once"
                ToolTip.delay: 400
                Accessible.name: "Select clips for batch save"
            }

            AppButton {
                icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
                flat: true
                implicitWidth: 40
                implicitHeight: 40
                rotation: SunoBridge.loading ? 360 : 0
                Behavior on rotation {
                    SequentialAnimation {
                        NumberAnimation { duration: 900; easing.type: Easing.InOutCubic }
                    }
                }
                ToolTip.visible: hovered
                ToolTip.text: "Refresh library"
                ToolTip.delay: 400
                onClicked: SunoBridge.refreshLibrary(1)
            }
        }

        // ── Selection / batch bar ──────────────────────
        // Appears only with something selected, so it cannot be mistaken for a
        // permanent toolbar. Named after the count it acts on.
        Rectangle {
            Layout.fillWidth: true
            visible: root.selectedCount > 0
            Layout.preferredHeight: visible ? 44 : 0
            color: Qt.lighter(Theme.backgroundAlt, 1.03)

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLarge
                anchors.rightMargin: Theme.spacingLarge
                spacing: Theme.spacingSmall

                Text {
                    text: root.selectedCount + " selected"
                    color: Theme.textPrimary
                    font: Theme.fontCaptionStrong
                    Layout.maximumWidth: parent.width - Theme.spacingLarge * 2 - 320
                    elide: Text.ElideRight
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Clear"
                    flat: true
                    onClicked: root.clearSelection()
                }

                AppButton {
                    text: root.selectedCount === 1
                          ? "Save 1 clip"
                          : "Save " + root.selectedCount + " clips"
                    highlighted: true
                    onClicked: root.saveClips(root.selectedIds)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        // ═════════════════════════════════════════════
        // CLIP GRID
        // ═════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: clipGrid
                anchors.fill: parent
                anchors.margins: Theme.spacingLarge
                clip: true

                model: SunoBridge.clips

                readonly property int columns: Math.max(2, Math.floor(width / Theme.cardTileMinimum))
                cellWidth: Math.floor(width / columns)
                cellHeight: cellWidth * 1.22 + Theme.spacingSmall

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: ClipCard {
                    width: clipGrid.cellWidth - Theme.spacingSmall
                    height: clipGrid.cellHeight - Theme.spacingSmall
                    clipData: modelData

                    selectionMode: root.selectionMode
                    selected: root.isSelected(modelData ? modelData.id : "")
                    saveState: root.saveStateFor(modelData ? modelData.id : "")
                    savedPath: root.savedPathFor(modelData ? modelData.id : "")
                    saveErrorText: root.saveErrorFor(modelData ? modelData.id : "")

                    // Center tile inside its cell
                    transform: Translate {
                        x: (clipGrid.cellWidth - Theme.spacingSmall - width) / 2
                        y: (clipGrid.cellHeight - Theme.spacingSmall - height) / 2
                    }

                    onOpened: function(openedClip) {
                        detailSheet.clipData = openedClip
                        detailSheet.open()
                    }

                    onSaveRequested: function(clipId) {
                        root.saveClips([clipId], clipId)
                    }

                    onSelectionToggled: function(clip) {
                        root.toggleSelection(clip)
                    }

                    onContextRequested: function(clip, globalPos) {
                        if (!clip)
                            return
                        contextMenu.clipData = clip
                        // Menu.popup(point) is relative to the popup's PARENT
                        // item, not the window — this Menu's parent is the
                        // LibraryView root — so the delegate's window
                        // coordinates have to come back through
                        // mapFromGlobal or the menu opens at the wrong place.
                        contextMenu.popup(root.mapFromGlobal(globalPos.x, globalPos.y))
                    }
                }

                onAtYEndChanged: {
                    if (atYEnd && SunoBridge.hasMorePages && !SunoBridge.loading)
                        SunoBridge.requestNextLibraryPage()
                }

                // Viewport-fill guard: if the grid is not scrollable (20
                // items in a tall window), atYEnd never fires. Auto-page
                // until the viewport is filled or the feed is exhausted.
                onCountChanged: tryFillViewport()
                onHeightChanged: tryFillViewport()
                function tryFillViewport() {
                    if (clipGrid.count > 0 && !SunoBridge.loading && SunoBridge.hasMorePages
                            && clipGrid.contentHeight <= clipGrid.height) {
                        Qt.callLater(function() {
                            if (!SunoBridge.loading && SunoBridge.hasMorePages)
                                SunoBridge.requestNextLibraryPage()
                        })
                    }
                }
                Connections {
                    target: SunoBridge
                    function onHasMorePagesChanged() { clipGrid.tryFillViewport() }
                    function onClipsChanged() { clipGrid.tryFillViewport() }
                }

                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.durationNormal }
                    NumberAnimation { property: "scale"; from: 0.92; to: 1.0; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
                }

                // ── Empty state ─────────────────────
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: clipGrid.count === 0 && !SunoBridge.loading
                    spacing: Theme.spacingSmall

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: clipGrid.width - Theme.spacingLarge * 2
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: {
                            if (root.query.length > 0) return "No tracks match your search"
                            if (!SunoBridge.isAuthenticated) return "Not signed in.\nSign in via Settings → Suno AI to load your library."
                            return "Library is empty.\nGenerate your first track or pull to refresh."
                        }
                        color: Theme.textDisabled
                        font: Theme.fontBody
                    }

                    AppButton {
                        visible: root.query.length === 0 && !SunoBridge.isAuthenticated
                        text: "Open Settings → Suno AI"
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: {
                            if (typeof mainWindow !== "undefined" && mainWindow.navigate)
                                mainWindow.navigate("settings")
                        }
                    }

                    AppButton {
                        visible: root.query.length === 0 && SunoBridge.isAuthenticated
                        text: "Refresh"
                        flat: true
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: SunoBridge.refreshLibrary(1)
                    }
                }

                // ── Initial load spinner ────────────
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: clipGrid.count === 0 && SunoBridge.loading
                    spacing: Theme.spacingSmall

                    BusyIndicator { Layout.alignment: Qt.AlignHCenter }
                    Text {
                        text: "Fetching your library…"
                        color: Theme.textSecondary
                        font: Theme.fontCaption
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }

        // ═════════════════════════════════════════════
        // FOOTER STRIP (pagination state)
        // ═════════════════════════════════════════════
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            color: Qt.lighter(Theme.backgroundAlt, 1.03)

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLarge
                anchors.rightMargin: Theme.spacingLarge

                Text {
                    text: {
                        if (SunoBridge.errorMessage.length > 0) return SunoBridge.errorMessage
                        if (SunoBridge.downloadStatus.length > 0) return SunoBridge.downloadStatus
                        if (SunoBridge.statusMessage.length > 0) return SunoBridge.statusMessage
                        if (SunoBridge.loading) return "Loading…"
                        return SunoBridge.hasMorePages
                                ? "Scroll for more · page " + SunoBridge.currentPage
                                : "End of library"
                    }
                    color: SunoBridge.errorMessage.length > 0
                           ? Theme.error : Theme.textDisabled
                    font: Theme.fontCaption
                    elide: Text.ElideRight
                    Layout.maximumWidth: parent.width - Theme.spacingLarge * 2 - 40
                }

                // "Show in folder" for the clip that just landed. Only offered
                // when there is a path to open, and it opens the FILE, so the
                // file manager selects it rather than showing the directory.
                AppButton {
                    visible: root.lastSavedPath.length > 0
                    text: "Show saved file"
                    flat: true
                    implicitHeight: 24
                    buttonRadius: Theme.radiusSmall
                    onClicked: Qt.openUrlExternally(root.localFileUrl(root.lastSavedPath))
                    ToolTip.visible: hovered
                    ToolTip.text: root.lastSavedPath
                    ToolTip.delay: 300
                    Accessible.name: "Show " + root.lastSavedPath + " in the file manager"
                }

                BusyIndicator {
                    visible: SunoBridge.loading && clipGrid.count > 0
                    running: visible
                    implicitHeight: 18
                    implicitWidth: 18
                }
            }
        }
    }

    ClipDetailSheet {
        id: detailSheet
        // The sheet's own save goes through the same view-level bookkeeping as
        // the cards, so a save started from the sheet leaves the card's button
        // in the matching state instead of two disagreeing halves of the truth.
        saveState: root.saveStateFor(detailSheet.clipData && detailSheet.clipData.id)
        savedPath: root.savedPathFor(detailSheet.clipData && detailSheet.clipData.id)
        saveErrorText: root.saveErrorFor(detailSheet.clipData && detailSheet.clipData.id)

        onSaveRequested: function(clipId) {
            root.saveClips([clipId], clipId)
        }
    }

    // ── Right-click menu ──────────────────────────
    // Anchored to a clip rather than to a selection, because it is the clip's
    // menu; the batch act above lives in the selection bar where the count is
    // visible.
    Menu {
        id: contextMenu

        property var clipData: null

        MenuItem {
            text: "Save to downloads"
            enabled: !!contextMenu.clipData
                     && contextMenu.clipData.status === "complete"
                     && contextMenu.clipData.has_media !== false
            onTriggered: root.saveClips(
                             [contextMenu.clipData.id], contextMenu.clipData.id)
        }

        MenuItem {
            text: "Play"
            enabled: !!contextMenu.clipData
                     && contextMenu.clipData.status === "complete"
                     && contextMenu.clipData.has_media !== false
            onTriggered: SunoBridge.playClip(contextMenu.clipData.id)
        }

        MenuSeparator {}

        MenuItem {
            text: "Details"
            enabled: !!contextMenu.clipData
            onTriggered: {
                detailSheet.clipData = contextMenu.clipData
                detailSheet.open()
            }
        }

        MenuItem {
            text: root.isSelected(contextMenu.clipData && contextMenu.clipData.id)
                  ? "Remove from selection"
                  : "Add to selection"
            enabled: !!contextMenu.clipData
            onTriggered: root.toggleSelection(contextMenu.clipData)
        }
    }

    Connections {
        target: SunoBridge
        // The per-clip result channel. Fires once per clip of a batch and
        // carries the path actually written, which is the only confirmation
        // that names a file rather than describing an event.
        function onClipSaved(clipId, savedPath) {
            root.setSaveState(clipId, "saved", savedPath, "")
        }

        // A transfer that dies mid-flight never reaches clipSaved, so its card would
        // sit on "Saving" forever. downloadStatus is the only string that
        // reports a terminal outcome, and it names the clip by TITLE rather than
        // id, so this match is exact-equality on the title and is
        // best-effort by construction: two saving clips sharing a title both
        // get marked. That is the safe direction — both really did fail — and
        // the honest alternative is a permanent "Saving" that means nothing.
        //
        // It marks them refused with the status sentence rather than back to
        // idle, because idle would render the failure as if nothing happened.
        function onDownloadStatusChanged() {
            const status = String(SunoBridge.downloadStatus)
            const prefixes = ["Download failed for ", "Download cancelled for "]
            let title = null
            for (let i = 0; i < prefixes.length; ++i) {
                if (status.indexOf(prefixes[i]) === 0) {
                    title = status.slice(prefixes[i].length)
                    break
                }
            }
            if (title === null)
                return
            const clips = SunoBridge.clips
            for (let i = 0; i < clips.length; ++i) {
                const clip = clips[i]
                if (!clip)
                    continue
                const entry = root.saveStates[clip.id]
                if (entry && entry.state === "saving"
                        && (clip.title || "clip") === title) {
                    root.setSaveState(clip.id, "refused", "", status)
                }
            }
        }
    }

    // Landing fetch: populate the default view if the session started cold
    Component.onCompleted: {
        if (!SunoBridge.loading && SunoBridge.clips.length === 0)
            SunoBridge.refreshLibrary(1)
    }

    // 15 s fallback: if Bridge misses a failure edge, clear stuck spinner.
    Timer {
        id: libraryWatchdog
        interval: 15000
        running: SunoBridge.loading
        repeat: false
        onTriggered: {
            if (SunoBridge.loading) {
                console.warn("LibraryView: watchdog clearing stuck loading after 15s")
                SunoBridge.clearLoading()
            }
        }
    }
}
