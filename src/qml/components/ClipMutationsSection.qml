import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis

/**
 * The mutation section of ClipDetailSheet (REACTIVE-GUI-PLAN §6.2).
 *
 * The SunoLibraryMutations verbs, grouped "Clip actions" vs
 * "Playlist actions", each control presented only when its owning
 * gate (gateFor()) permits it:
 *   Library gate  — set-visibility, toggle-remix-permission,
 *                   toggle-show-remixes, update-feedback-state,
 *                   clip-trash-restore
 *   Sharing gate  — share-link
 *   Playlists gate— set-metadata, update-clips, tracks-add,
 *                   tracks-remove, reorder, cover-image,
 *                   playlist-trash-v2
 * A verb whose gate denies is ABSENT, never disabled-with-a-mystery.
 * When nothing at all is permitted, mutationsUnavailableReason is
 * shown verbatim instead of an invented sentence.
 *
 * State honesty: toggles show CONFIRMED state only. Visibility is
 * seeded from the clip's own is_public (the feed field, exposed by
 * the bridge); remix/show-remixes are not reported by any clip
 * payload, so they read "Unknown" until a change is accepted in
 * this sheet, and each verb is offered as explicit action buttons
 * ("Allow remixes" / "Disallow remixes") rather than a toggle that
 * would guess. Nothing flips optimistically: known state changes
 * only on an `accepted` settle, and accepted settles trigger the
 * bridge's libraryRefreshRecommended -> refreshLibrary(1) re-read.
 *
 * Allowed verb 10 (playlist-tracks-reorder) is NOT wired here:
 * positions need track indices the bridge does not expose
 * (playlistClips carries no relative_index), and a guessed index
 * would corrupt order. Verb 7 (playlist-update-clips) is wired with
 * an empty metadata object and says so: the capture proves metadata
 * is an object and names no key inside it.
 *
 * This is sheet-scoped state, never delegate state: the section is
 * instantiated once in the sheet, not per card.
 */
Item {
    id: root

    // The clip the sheet is showing. Empty = no clip; the section
    // renders nothing rather than guessing a subject.
    property string clipId: ""
    property string clipTitle: ""
    // Open playlist context, passed through from SunoBridge by the
    // sheet (§6.2: never guessed from the grid). "" = no playlist.
    property string playlistId: ""
    // Visibility as reported by the feed (clipData.is_public):
    // -1 unknown, 0 private, 1 public.
    property int seedVisibility: -1

    property bool expanded: false

    // ── Session-confirmed state ─────────────────────────────
    // Accepted-request values, sheet lifetime. Reset per clip.
    property int acceptedVisibility: -1
    property int acceptedRemix: -1
    property int acceptedShow: -1
    // Accepted trash state, sheet lifetime. Reset per clip / playlist.
    property int acceptedTrashed: -1
    property int acceptedPlaylistTrashed: -1
    // The verb string awaiting its settle ("set-visibility", ...).
    property string pendingVerb: ""
    // verb -> value we last asked for, read back at settle so the
    // confirmed state is the request the server accepted, never a
    // field plucked from an unparsed response body.
    property var requested: ({})

    property string lastMessage: ""
    property bool lastIsError: false

    onClipIdChanged: {
        acceptedVisibility = -1
        acceptedRemix = -1
        acceptedShow = -1
        acceptedTrashed = -1
        acceptedPlaylistTrashed = -1
        pendingVerb = ""
        lastMessage = ""
        lastIsError = false
        requested = ({})
    }

    readonly property int visibilityState: acceptedVisibility >= 0
                                           ? acceptedVisibility : seedVisibility
    readonly property bool libraryAvailable: root.gateAvailable("library")
    readonly property bool sharingAvailable: root.gateAvailable("sharing")
    readonly property bool playlistsAvailable: root.gateAvailable("playlists")
    readonly property bool playlistContext: playlistId.length > 0
    readonly property bool anyPermitted: libraryAvailable || sharingAvailable
                                         || (playlistContext && playlistsAvailable)
    readonly property bool busy: SunoBridge.mutationsBusy

    function gateAvailable(gateName) {
        const rows = SunoBridge.gateStatuses
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].gate === gateName)
                return rows[i].available === true
        }
        return false
    }

    function showMessage(message, isError) {
        lastMessage = message
        lastIsError = isError
    }

    function stateWord(state) {
        if (state === 1) return "yes"
        if (state === 0) return "no"
        return "unknown"
    }

    function ask(verb, value, invoke) {
        requested[verb] = value
        pendingVerb = verb
        lastMessage = ""
        invoke()
    }

    function onSettled(verb, outcome, reason) {
        if (pendingVerb === verb)
            pendingVerb = ""
        if (outcome === "accepted") {
            if (verb === "set-visibility")
                acceptedVisibility = requested[verb] === true ? 1 : 0
            if (verb === "toggle-remix-permission")
                acceptedRemix = requested[verb] === true ? 1 : 0
            if (verb === "toggle-show-remixes")
                acceptedShow = requested[verb] === true ? 1 : 0
            if (verb === "clip-trash-restore")
                acceptedTrashed = requested[verb] === true ? 1 : 0
            if (verb === "playlist-trash-v2")
                acceptedPlaylistTrashed = requested[verb] === true ? 1 : 0
            if (verb === "set-visibility")
                showMessage(requested[verb] === true ? "Made public." : "Made private.", false)
            else if (verb === "toggle-remix-permission")
                showMessage(requested[verb] === true ? "Remixes allowed." : "Remixes disallowed.", false)
            else if (verb === "toggle-show-remixes")
                showMessage(requested[verb] === true ? "Remixes will be shown." : "Remixes are hidden.", false)
            else if (verb === "share-link")
                showMessage("Share link accepted, but this build does not show the link.", false)
            else if (verb === "update-feedback-state")
                showMessage("Feedback sent.", false)
            else if (verb === "playlist-set-metadata")
                showMessage("Playlist details saved.", false)
            else if (verb === "playlist-tracks-add" || verb === "playlist-update-clips")
                showMessage("Added to the playlist.", false)
            else if (verb === "playlist-tracks-remove")
                showMessage("Removed from the playlist.", false)
            else if (verb === "playlist-cover-image")
                showMessage("Cover image set.", false)
            else if (verb === "clip-trash-restore")
                showMessage(requested[verb] === true ? "Moved to trash." : "Restored.", false)
            else if (verb === "playlist-trash-v2")
                showMessage(requested[verb] === true ? "Playlist moved to trash." : "Playlist restored.", false)
            else
                showMessage("Updated.", false)
            if (verb.indexOf("playlist-") === 0)
                SunoBridge.refreshPlaylists()
        } else {
            // refused / rejected / failed: the client's or server's
            // own sentence, verbatim; never an HTTP code, never the
            // verb enum, never a bare "failed".
            showMessage(reason.length > 0 ? reason : "That change was not saved.", true)
            root.shakeRow(verb)
        }
    }

    function shakeRow(verb) {
        const row = root.rowForVerb(verb)
        if (row)
            row.shake()
    }

    function rowForVerb(verb) {
        switch (verb) {
        case "set-visibility": return visRow
        case "toggle-remix-permission": return remixRow
        case "toggle-show-remixes": return showRow
        case "share-link": return shareRow
        case "update-feedback-state": return feedbackRow
        case "playlist-set-metadata": return metaRow
        case "playlist-tracks-add": return tracksRow
        case "playlist-tracks-remove": return tracksRow
        case "playlist-update-clips": return updateRow
        case "playlist-cover-image": return coverRow
        case "clip-trash-restore": return trashRow
        case "playlist-trash-v2": return playlistTrashRow
        }
        return null
    }

    Connections {
        target: SunoBridge
        ignoreUnknownSignals: true

        function onClipMutationSettled(subjectId, verb, outcome, reason) {
            if (subjectId !== root.clipId)
                return
            root.onSettled(verb, outcome, reason)
        }

        function onPlaylistMutationSettled(subjectId, verb, outcome, reason) {
            if (!root.playlistContext || subjectId !== root.playlistId)
                return
            root.onSettled(verb, outcome, reason)
        }

        // Accepted means the server took it; nothing was parsed back
        // and nothing written locally, so the displayed state is
        // stale until the library is re-read.
        function onLibraryRefreshRecommended() {
            SunoBridge.refreshLibrary(1)
        }
    }

    // ── One mutation row ────────────────────────────────────
    // A label, the confirmed state in words, and explicit action
    // buttons. A row never animates its own label/state bindings;
    // shake() is the only motion and it is failure-only (§4.3).
    component MutationRow: ColumnLayout {
        id: row

        property string verbKey: ""
        property string title: ""
        property string stateText: ""
        property string labelA: ""
        property string labelB: ""
        property bool confirmedA: false
        property bool confirmedB: false
        property bool available: true
        signal actionA
        signal actionB

        spacing: Theme.spacingTiny
        Layout.fillWidth: true
        transformOrigin: Item.Center

        Text {
            Layout.fillWidth: true
            text: row.title + "  ·  " + row.stateText
            color: Theme.textPrimary
            font: Theme.fontCaptionStrong
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            AppButton {
                text: root.pendingVerb === row.verbKey ? row.labelA + "…" : row.labelA
                flat: !row.confirmedA
                highlighted: row.confirmedA
                enabled: row.available && !root.busy && root.pendingVerb !== row.verbKey
                onClicked: row.actionA()
                Accessible.name: row.labelA + " for " + root.clipTitle
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: row.labelA
            }

            AppButton {
                visible: row.labelB.length > 0
                text: root.pendingVerb === row.verbKey ? row.labelB + "…" : row.labelB
                flat: !row.confirmedB
                highlighted: row.confirmedB
                enabled: row.available && !root.busy && root.pendingVerb !== row.verbKey
                onClicked: row.actionB()
                Accessible.name: row.labelB + " for " + root.clipTitle
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: row.labelB
            }
        }

        function shake() {
            shakeAnim1.restart()
        }

        SequentialAnimation {
            id: shakeAnim1
            NumberAnimation { target: row; property: "rotation"; to: -3; duration: 70; easing.type: Easing.InOutQuad }
            NumberAnimation { target: row; property: "rotation"; to: 3; duration: 70; easing.type: Easing.InOutQuad }
            NumberAnimation { target: row; property: "rotation"; to: -2; duration: 70; easing.type: Easing.InOutQuad }
            NumberAnimation { target: row; property: "rotation"; to: 0; duration: 70; easing.type: Easing.InOutQuad }
        }
    }

    implicitHeight: body.implicitHeight

    ColumnLayout {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingSmall

        AppButton {
            Layout.fillWidth: true
            flat: true
            text: root.expanded ? "▾ Actions" : "▸ Actions"
            enabled: root.clipId.length > 0
            onClicked: root.expanded = !root.expanded
            Accessible.name: root.expanded ? "Hide clip actions" : "Show clip actions"
        }

        ColumnLayout {
            visible: root.expanded && root.clipId.length > 0
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            // Last settled outcome, in plain words. Never an HTTP
            // code or a verb enum; failures carry the reason verbatim.
            Text {
                Layout.fillWidth: true
                visible: root.lastMessage.length > 0
                text: root.lastMessage
                color: root.lastIsError ? Theme.error : Theme.success
                font: Theme.fontCaption
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: !root.anyPermitted && SunoBridge.mutationsUnavailableReason.length > 0
                text: SunoBridge.mutationsUnavailableReason
                color: Theme.textSecondary
                font: Theme.fontCaption
                wrapMode: Text.WordWrap
            }

            // ── Clip actions (Library + Sharing gates) ──────
            SectionHeader {
                visible: root.libraryAvailable || root.sharingAvailable
                text: "Clip"
            }

            MutationRow {
                id: visRow
                visible: root.libraryAvailable
                verbKey: "set-visibility"
                title: "Visibility"
                stateText: root.visibilityState === 1 ? "Public"
                           : root.visibilityState === 0 ? "Private"
                           : "State unknown"
                labelA: "Make public"
                labelB: "Make private"
                confirmedA: root.visibilityState === 1
                confirmedB: root.visibilityState === 0
                enabled: root.libraryAvailable
                onActionA: root.ask("set-visibility", true,
                                     function() { SunoBridge.setClipVisibility(root.clipId, true) })
                onActionB: root.ask("set-visibility", false,
                                     function() { SunoBridge.setClipVisibility(root.clipId, false) })
            }

            MutationRow {
                id: remixRow
                visible: root.libraryAvailable
                verbKey: "toggle-remix-permission"
                title: "Remixes"
                stateText: root.stateWord(root.acceptedRemix) === "unknown"
                           ? "Permission unknown"
                           : "Remixes " + root.stateWord(root.acceptedRemix)
                labelA: "Allow remixes"
                labelB: "Disallow remixes"
                confirmedA: root.acceptedRemix === 1
                confirmedB: root.acceptedRemix === 0
                enabled: root.libraryAvailable
                onActionA: root.ask("toggle-remix-permission", true,
                                     function() { SunoBridge.setClipRemixPermission(root.clipId, true) })
                onActionB: root.ask("toggle-remix-permission", false,
                                     function() { SunoBridge.setClipRemixPermission(root.clipId, false) })
            }

            MutationRow {
                id: showRow
                visible: root.libraryAvailable
                verbKey: "toggle-show-remixes"
                title: "Show remixes"
                stateText: root.stateWord(root.acceptedShow) === "unknown"
                           ? "Unknown"
                           : "Remixes " + root.stateWord(root.acceptedShow)
                labelA: "Show remixes"
                labelB: "Hide remixes"
                confirmedA: root.acceptedShow === 1
                confirmedB: root.acceptedShow === 0
                enabled: root.libraryAvailable
                onActionA: root.ask("toggle-show-remixes", true,
                                     function() { SunoBridge.setClipShowRemixes(root.clipId, true) })
                onActionB: root.ask("toggle-show-remixes", false,
                                     function() { SunoBridge.setClipShowRemixes(root.clipId, false) })
            }

            MutationRow {
                id: shareRow
                visible: root.sharingAvailable
                verbKey: "share-link"
                title: "Share"
                stateText: "Mint a public link"
                labelA: "Get share link"
                labelB: ""
                confirmedA: false
                confirmedB: false
                enabled: root.sharingAvailable
                onActionA: root.ask("share-link", "clip",
                                     function() { SunoBridge.shareClip(root.clipId, "clip") })
                onActionB: {}
            }

            MutationRow {
                id: trashRow
                visible: root.libraryAvailable
                verbKey: "clip-trash-restore"
                title: "Trash"
                stateText: root.stateWord(root.acceptedTrashed) === "unknown"
                           ? "Trash state unknown"
                           : root.acceptedTrashed === 1 ? "In trash" : "Not trashed"
                labelA: "Move to trash"
                labelB: "Restore"
                confirmedA: root.acceptedTrashed === 1
                confirmedB: root.acceptedTrashed === 0
                enabled: root.libraryAvailable
                onActionA: root.ask("clip-trash-restore", true,
                                     function() { SunoBridge.setClipTrashed([root.clipId], true) })
                onActionB: root.ask("clip-trash-restore", false,
                                     function() { SunoBridge.setClipTrashed([root.clipId], false) })
            }

            // Feedback: the capture proves feedback_reason is a
            // string and names no member list, so the reason is the
            // user's own text, passed through verbatim.
            ColumnLayout {
                id: feedbackRow
                visible: root.libraryAvailable
                Layout.fillWidth: true
                spacing: Theme.spacingTiny
                transformOrigin: Item.Center

                Text {
                    Layout.fillWidth: true
                    text: "Feedback"
                    color: Theme.textPrimary
                    font: Theme.fontCaptionStrong
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    AppTextField {
                        id: feedbackField
                        Layout.fillWidth: true
                        placeholderText: "Feedback reason"
                        color: Theme.textPrimary
                        font: Theme.fontCaption
                        padding: Theme.spacingSmall
                        enabled: root.libraryAvailable && !root.busy
                        Accessible.name: "Feedback reason for " + root.clipTitle
                    }

                    AppButton {
                        text: root.pendingVerb === "update-feedback-state" ? "Sending…" : "Send feedback"
                        enabled: root.libraryAvailable && !root.busy
                                 && feedbackField.text.trim().length > 0
                        onClicked: {
                            const reason = feedbackField.text.trim()
                            feedbackField.clear()
                            root.ask("update-feedback-state", reason,
                                     function() { SunoBridge.setClipFeedback(root.clipId, reason) })
                        }
                        Accessible.name: "Send feedback for " + root.clipTitle
                    }
                }

                function shake() { shakeAnim2.restart() }

                SequentialAnimation {
                    id: shakeAnim2
                    NumberAnimation { target: feedbackRow; property: "rotation"; to: -3; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: feedbackRow; property: "rotation"; to: 3; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: feedbackRow; property: "rotation"; to: -2; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: feedbackRow; property: "rotation"; to: 0; duration: 70; easing.type: Easing.InOutQuad }
                }
            }

            // ── Playlist actions (Playlists gate + context) ─
            SectionHeader {
                visible: root.playlistContext && root.playlistsAvailable
                text: "Playlist"
            }

            ColumnLayout {
                id: metaRow
                visible: root.playlistContext && root.playlistsAvailable
                Layout.fillWidth: true
                spacing: Theme.spacingTiny
                transformOrigin: Item.Center

                Text {
                    Layout.fillWidth: true
                    text: "Playlist details"
                    color: Theme.textPrimary
                    font: Theme.fontCaptionStrong
                }

                AppTextField {
                    id: playlistNameField
                    Layout.fillWidth: true
                    placeholderText: "Name"
                    color: Theme.textPrimary
                    font: Theme.fontCaption
                    padding: Theme.spacingSmall
                    enabled: root.playlistsAvailable && !root.busy
                    Accessible.name: "Playlist name"
                    Component.onCompleted: {
                        if (SunoBridge.openPlaylistId === root.playlistId)
                            text = SunoBridge.openPlaylistName
                    }
                }

                AppTextField {
                    id: playlistDescField
                    Layout.fillWidth: true
                    placeholderText: "Description"
                    color: Theme.textPrimary
                    font: Theme.fontCaption
                    padding: Theme.spacingSmall
                    enabled: root.playlistsAvailable && !root.busy
                    Accessible.name: "Playlist description"
                }

                AppButton {
                    text: root.pendingVerb === "playlist-set-metadata" ? "Saving…" : "Save details"
                    enabled: root.playlistsAvailable && !root.busy
                             && playlistNameField.text.trim().length > 0
                    onClicked: {
                        const name = playlistNameField.text.trim()
                        const desc = playlistDescField.text
                        root.ask("playlist-set-metadata", name,
                                 function() { SunoBridge.setPlaylistMetadata(root.playlistId, name, desc) })
                    }
                    Accessible.name: "Save playlist details"
                }

                function shake() { shakeAnim3.restart() }

                SequentialAnimation {
                    id: shakeAnim3
                    NumberAnimation { target: metaRow; property: "rotation"; to: -3; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: metaRow; property: "rotation"; to: 3; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: metaRow; property: "rotation"; to: -2; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: metaRow; property: "rotation"; to: 0; duration: 70; easing.type: Easing.InOutQuad }
                }
            }

            MutationRow {
                id: tracksRow
                visible: root.playlistContext && root.playlistsAvailable
                verbKey: "playlist-tracks-add"
                title: "This clip"
                stateText: root.playlistContext ? "Added to the open playlist on demand" : ""
                labelA: "Add to playlist"
                labelB: "Remove from playlist"
                confirmedA: false
                confirmedB: false
                enabled: root.playlistContext && root.playlistsAvailable
                onActionA: root.ask("playlist-tracks-add", [root.clipId],
                                     function() { SunoBridge.addClipsToPlaylist(root.playlistId, [root.clipId]) })
                onActionB: root.ask("playlist-tracks-remove", [root.clipId],
                                     function() { SunoBridge.removeClipsFromPlaylist(root.playlistId, [root.clipId]) })
            }

            ColumnLayout {
                id: updateRow
                visible: root.playlistContext && root.playlistsAvailable
                Layout.fillWidth: true
                spacing: Theme.spacingTiny
                transformOrigin: Item.Center

                Text {
                    Layout.fillWidth: true
                    text: "Change kind"
                    color: Theme.textPrimary
                    font: Theme.fontCaptionStrong
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    AppComboBox {
                        id: updateKindBox
                        Layout.fillWidth: true
                        model: ["add", "remove", "remove_by_id", "reorder"]
                        enabled: root.playlistsAvailable && !root.busy
                        Accessible.name: "Playlist change kind"
                    }

                    AppButton {
                        text: root.pendingVerb === "playlist-update-clips" ? "Applying…" : "Apply"
                        enabled: root.playlistsAvailable && !root.busy
                        onClicked: root.ask("playlist-update-clips", updateKindBox.currentText,
                                             function() { SunoBridge.updatePlaylistClips(root.playlistId, updateKindBox.currentText, ({})) })
                        Accessible.name: "Apply playlist change"
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "Sent with empty metadata; its contents were not captured."
                    color: Theme.textDisabled
                    font: Theme.fontTiny
                    wrapMode: Text.WordWrap
                }

                function shake() { shakeAnim4.restart() }

                SequentialAnimation {
                    id: shakeAnim4
                    NumberAnimation { target: updateRow; property: "rotation"; to: -3; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: updateRow; property: "rotation"; to: 3; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: updateRow; property: "rotation"; to: -2; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: updateRow; property: "rotation"; to: 0; duration: 70; easing.type: Easing.InOutQuad }
                }
            }

            ColumnLayout {
                id: coverRow
                visible: root.playlistContext && root.playlistsAvailable
                Layout.fillWidth: true
                spacing: Theme.spacingTiny
                transformOrigin: Item.Center

                Text {
                    Layout.fillWidth: true
                    text: "Cover image"
                    color: Theme.textPrimary
                    font: Theme.fontCaptionStrong
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    AppTextField {
                        id: coverField
                        Layout.fillWidth: true
                        placeholderText: "Image id"
                        color: Theme.textPrimary
                        font: Theme.fontCaption
                        padding: Theme.spacingSmall
                        enabled: root.playlistsAvailable && !root.busy
                        Accessible.name: "Cover image id"
                    }

                    AppButton {
                        text: root.pendingVerb === "playlist-cover-image" ? "Setting…" : "Set cover"
                        enabled: root.playlistsAvailable && !root.busy
                                 && coverField.text.trim().length > 0
                        onClicked: {
                            const imageId = coverField.text.trim()
                            root.ask("playlist-cover-image", imageId,
                                     function() { SunoBridge.setPlaylistCoverImage(root.playlistId, imageId) })
                        }
                        Accessible.name: "Set playlist cover image"
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "Needs an image id, not a clip id."
                    color: Theme.textDisabled
                    font: Theme.fontTiny
                    wrapMode: Text.WordWrap
                }

                function shake() { shakeAnim5.restart() }

                SequentialAnimation {
                    id: shakeAnim5
                    NumberAnimation { target: coverRow; property: "rotation"; to: -3; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: coverRow; property: "rotation"; to: 3; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: coverRow; property: "rotation"; to: -2; duration: 70; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: coverRow; property: "rotation"; to: 0; duration: 70; easing.type: Easing.InOutQuad }
                }
            }

            MutationRow {
                id: playlistTrashRow
                visible: root.playlistContext && root.playlistsAvailable
                verbKey: "playlist-trash-v2"
                title: "Playlist trash"
                stateText: root.stateWord(root.acceptedPlaylistTrashed) === "unknown"
                           ? "Trash state unknown"
                           : root.acceptedPlaylistTrashed === 1 ? "In trash" : "Not trashed"
                labelA: "Move playlist to trash"
                labelB: "Restore playlist"
                confirmedA: root.acceptedPlaylistTrashed === 1
                confirmedB: root.acceptedPlaylistTrashed === 0
                enabled: root.playlistContext && root.playlistsAvailable
                onActionA: root.ask("playlist-trash-v2", true,
                                     function() { SunoBridge.setPlaylistTrashed(root.playlistId, true) })
                onActionB: root.ask("playlist-trash-v2", false,
                                     function() { SunoBridge.setPlaylistTrashed(root.playlistId, false) })
            }
        }
    }
}
