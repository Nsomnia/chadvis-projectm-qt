import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ChadVis
import "../components"
import "../panels/settings"

Flickable {
    id: root

    property bool signingOut: false
    property bool manualExpanded: false
    property bool showGateNotes: false
    property string feedbackMessage: ""
    property bool feedbackIsError: false

    readonly property var bridgeApi: SunoBridge
    readonly property string googleLoginState: {
        const state = bridgeApi["googleLoginState"]
        return typeof state === "string" ? state : "signedOut"
    }
    readonly property string googleLoginError: {
        const error = bridgeApi["googleLoginError"]
        return typeof error === "string" ? error : ""
    }
    readonly property bool googleLoginAvailable:
        typeof bridgeApi["googleLoginAvailable"] === "boolean"
        && bridgeApi["googleLoginAvailable"] === true
    readonly property bool cancelGoogleLoginAvailable:
        bridgeSupports("cancelGoogleSignIn")
    readonly property string visibleFeedback: {
        if (googleLoginState === "cancelled")
            return ""
        if (googleLoginState === "error") {
            return googleLoginError.length > 0
                    ? googleLoginError
                    : "Suno sign-in could not be completed."
        }
        return feedbackMessage
    }
    readonly property bool visibleFeedbackIsError:
        googleLoginState === "error" || feedbackIsError

    // What the field shows, resolved once so the button, the caption and the
    // browse dialog cannot disagree about it.
    readonly property string downloadFolder: String(SettingsBridge.sunoDownloadPath)

    // ── Capability diagnostics ────────────────────────────────────────────────
    // SunoBridge.gateStatuses is one QVariantMap per gated surface. The binding
    // below is the whole data dependency: it re-runs only when the bridge emits
    // gateStatusesChanged, which SunoAccountManager does on every gatesChanged
    // (a fetched session envelope, and a sign-out clearing it). Nothing here is
    // per-frame, so the ~27-row table is walked once per change and never again.
    //
    // `showGateNotes` is a parameter of the build rather than something the row
    // delegate reaches for. That is deliberate: the notes are a paragraph each,
    // roughly 2,700 words across the catalog, and threading the flag through the
    // model is what keeps the delegate from needing a reference back out to this
    // object. Rebuilding 27 small objects on an explicit button press is not a
    // cost worth optimising away.
    readonly property var gateModel: root.buildGateModel(SunoBridge.gateStatuses,
                                                         root.showGateNotes)

    // Areas in the order a reader wants them: the library and generation
    // surfaces this client is built around first, `labs` last because a lab is
    // by definition not product. Anything not in this list still appears — see
    // buildGateModel.
    readonly property var gateAreaOrder: ["library", "generation", "account", "media",
                                          "discover", "community", "projects", "player",
                                          "app", "labs"]
    readonly property var gateAreaLabels: {
        "library": "Library",
        "generation": "Generation",
        "account": "Account",
        "media": "Playback and downloads",
        "discover": "Discover",
        "community": "Community",
        "projects": "Projects",
        "player": "Player",
        "app": "Server-driven interface",
        "labs": "Experiments"
    }

    contentHeight: pageLayout.implicitHeight + Theme.spacingXL * 2
    clip: true

    function bridgeSupports(method) {
        return typeof bridgeApi[method] === "function"
    }

    function showFeedback(message, isError) {
        feedbackMessage = message
        feedbackIsError = isError
    }

    function beginGoogleSignIn() {
        feedbackMessage = ""
        feedbackIsError = false
        if (googleLoginAvailable && bridgeSupports("beginGoogleSignIn"))
            bridgeApi["beginGoogleSignIn"]()
    }

    function cancelGoogleSignIn() {
        if (googleLoginState === "browserOpen" && cancelGoogleLoginAvailable)
            bridgeApi["cancelGoogleSignIn"]()
    }

    function signOutSuno() {
        feedbackMessage = ""
        feedbackIsError = false
        if (!bridgeSupports("signOutSuno")) {
            showFeedback("Sign out is unavailable in this build.", true)
            return
        }

        signingOut = true
        bridgeApi["signOutSuno"]()
    }

    // QML's url value type has no toLocalFile(), so the reverse is built by
    // hand: normalize separators, then percent-encode. Naive "file://" + path
    // concatenation corrupts spaces and '#'/'?', and a Windows drive path needs
    // the extra slash. Same shape as PlaylistPanel's localFileUrl().
    function localFileUrl(path) {
        const p = String(path).replace(/\\/g, "/")
        return p.startsWith("/") ? "file://" + encodeURI(p)
                                 : "file:///" + encodeURI(p)
    }

    // FolderDialog hands back a url; strip the scheme and percent-decode, and
    // drop the extra slash before a Windows drive letter. Mirrors
    // RecordingPanel's toLocalFilePath().
    function toLocalPath(url) {
        const s = url.toString()
        if (!s.startsWith("file://"))
            return ""
        let path = decodeURIComponent(s.slice("file://".length))
        if (/^\/[A-Za-z]:\//.test(path))
            path = path.slice(1)
        return path
    }

    // ── Capability diagnostics: data shaping ──────────────────────────────────
    // One pass over the rows produces the counts, the ordered areas and the flat
    // list the Repeater consumes, so the table is walked exactly once per change.
    //
    // The output is deliberately FLAT — area headers and rows interleaved in one
    // array — rather than a header model whose rows a second Repeater walks. Two
    // reasons: a nested delegate has to reach back out to its parent's model for
    // the row count and the notes flag, which is the kind of outer-scope
    // reference that becomes a warning the moment someone adds
    // `pragma ComponentBehavior: Bound` to this file; and one delegate means the
    // row stays a component instead of becoming a 200-line inline block.
    function buildGateModel(rows, notesVisible) {
        const counts = {
            total: rows.length,
            available: 0,
            "server-gated": 0,
            "locally-disabled": 0,
            "evidence-blocked": 0,
            excluded: 0
        }
        const buckets = {}
        for (let i = 0; i < rows.length; ++i) {
            const status = String(rows[i].status)
            if (counts[status] !== undefined)
                counts[status] += 1
            const rawArea = rows[i].area
            const area = rawArea !== undefined && String(rawArea).length > 0
                         ? String(rawArea) : "other"
            if (buckets[area] === undefined)
                buckets[area] = []
            buckets[area].push(rows[i])
        }

        const ordered = []
        for (let i = 0; i < root.gateAreaOrder.length; ++i) {
            const area = root.gateAreaOrder[i]
            if (buckets[area] !== undefined) {
                ordered.push({ label: root.gateAreaLabels[area], rows: buckets[area] })
                delete buckets[area]
            }
        }
        // An area this page has never heard of is appended, not dropped. Silently
        // hiding a surface would be the one failure this panel cannot afford: it
        // exists to explain an absence, so it must not create one.
        const unknown = Object.keys(buckets).sort()
        for (let i = 0; i < unknown.length; ++i) {
            ordered.push({ label: root.humanAreaName(unknown[i]), rows: buckets[unknown[i]] })
        }

        const flat = []
        for (let g = 0; g < ordered.length; ++g) {
            const group = ordered[g]
            flat.push({ kind: "header", label: group.label, count: group.rows.length })
            for (let r = 0; r < group.rows.length; ++r) {
                flat.push({ kind: "row", data: group.rows[r],
                            statusText: root.gateStatusLabel(group.rows[r].status),
                            notes: notesVisible,
                            divider: r < group.rows.length - 1 })
            }
        }

        // The counts lead, and the order is the argument: what the account can
        // already use, then what the server has not enabled for it, then this
        // build's own switch, and last the two states a reader can do nothing
        // about — blocked on a capture nobody has taken, and excluded on purpose.
        const parts = []
        if (counts.available > 0)
            parts.push(counts.available + " available now")
        if (counts["server-gated"] > 0)
            parts.push(counts["server-gated"] + " not enabled for this account")
        if (counts["locally-disabled"] > 0)
            parts.push(counts["locally-disabled"] + " off in this build")
        if (counts["evidence-blocked"] > 0)
            parts.push(counts["evidence-blocked"] + " blocked on evidence")
        if (counts.excluded > 0)
            parts.push(counts.excluded + " excluded by policy")

        return { flat: flat, counts: counts, summary: parts.join("  ·  ") }
    }

    // A status as a reader would say it, not as the enum spells it. The label is
    // the accessible half of the non-colour channel: the marker shape is the
    // other half, and either alone is enough to tell the states apart.
    //
    // Called from buildGateModel rather than from inside GateStatusRow, because an
    // inline component has no business reaching back into this object for it, and
    // because resolving the label once per model build is cheaper than resolving
    // it once per row.
    function gateStatusLabel(status) {
        switch (String(status)) {
        case "available":
            return "Available"
        case "server-gated":
            return "Not enabled for this account"
        case "locally-disabled":
            return "Off in this build"
        case "evidence-blocked":
            return "Blocked on evidence"
        case "excluded":
            return "Excluded by policy"
        }
        return "Status unknown"
    }

    function humanAreaName(area) {
        const spaced = String(area).replace(/-/g, " ")
        return spaced.charAt(0).toUpperCase() + spaced.slice(1)
    }

    // ── Capability diagnostics: components ────────────────────────────────────
    //
    // A row's status is carried three ways, never by hue alone: a silhouette, a
    // word, and colour as reinforcement. The five shapes below are separable at
    // 12 px in a greyscale screenshot — filled dot, ring, square, diamond, bar —
    // and the grouping matters as much as the individual shapes. `excluded` and
    // `evidence-blocked` are drawn apart from `server-gated` and
    // `locally-disabled` because they mean the opposite thing: those two are
    // somebody else's switch, these two are this project's decision.
    component GateStatusMarker: Item {
        id: marker

        property string status: ""

        implicitWidth: Theme.iconSmall
        implicitHeight: Theme.iconSmall

        // available — a filled dot. Quiet on purpose: this is the resting state,
        // not the achievement, so it gets the calmest colour in the set.
        Rectangle {
            anchors.centerIn: parent
            width: Theme.spacingSmall + Theme.spacingTiny
            height: width
            radius: width / 2
            color: Theme.successDim
            visible: marker.status === "available"
        }

        // server-gated — a ring: something is there, it is simply not filled in.
        Rectangle {
            anchors.centerIn: parent
            width: Theme.spacingSmall + Theme.spacingTiny
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.textSecondary
            visible: marker.status === "server-gated"
        }

        // locally-disabled — a square: ours, and off.
        Rectangle {
            anchors.centerIn: parent
            width: Theme.spacingSmall + Theme.spacingTiny
            height: width
            radius: Theme.radiusSmall
            color: "transparent"
            border.width: 2
            border.color: Theme.textSecondary
            visible: marker.status === "locally-disabled"
        }

        // evidence-blocked — a diamond: a policy hold, and the only state here
        // worth pulling the eye to, because it is a refusal the project chose.
        Rectangle {
            anchors.centerIn: parent
            width: Theme.spacingSmall + Theme.spacingTiny
            height: width
            radius: Theme.radiusSmall
            rotation: 45
            color: "transparent"
            border.width: 2
            border.color: Theme.warningDim
            visible: marker.status === "evidence-blocked"
        }

        // excluded — a bar: struck out, and greyed because it is settled. A
        // capture landing tomorrow does not reopen it, so it must not look like
        // something pending.
        Rectangle {
            anchors.centerIn: parent
            width: Theme.spacingSmall + Theme.spacingTiny
            height: Theme.spacingTiny
            radius: height / 2
            color: Theme.textDisabled
            visible: marker.status === "excluded"
        }
    }

    // One surface, one verdict, one reason in full. The reason is never elided:
    // it is written to name the actual cause, and a truncated sentence throws
    // away the only part worth reading.
    component GateStatusRow: Item {
        id: entry

        // `entryData`, not `modelData`: this is instantiated as a plain child of
        // the delegate rather than as the delegate itself, and naming its own
        // property `modelData` would shadow the Repeater role of that name in the
        // caller's scope for no benefit.
        required property var entryData
        // Precomputed by buildGateModel — see the note on gateStatusLabel.
        property string statusText: ""
        property bool notesVisible: false
        property bool showDivider: false

        readonly property string surfaceTitle: {
            const title = entryData.title
            return title !== undefined && String(title).length > 0
                   ? String(title) : String(entryData.gate)
        }
        readonly property string reasonText: {
            const reason = entryData.reason
            // Empty is not expected — the resolver writes a sentence for all six
            // verdicts — but a bare "Disabled" would be a worse answer than an
            // honest admission that nothing was reported.
            return reason !== undefined && String(reason).length > 0
                   ? String(reason)
                   : "No reason was reported for this surface."
        }
        readonly property string metaText: {
            const evidence = entryData.evidence
            let meta = "Evidence: "
                       + (evidence !== undefined && String(evidence).length > 0
                          ? String(evidence) : "unknown")
            const flags = entryData.serverFlags
            if (flags !== undefined && String(flags).length > 0)
                meta += "  ·  Flags: " + String(flags)
            return meta
        }
        readonly property string noteText: entryData.note !== undefined
                                           ? String(entryData.note) : ""
        readonly property color statusColor: {
            switch (String(entryData.status)) {
            case "available":
                return Theme.successDim
            case "evidence-blocked":
                return Theme.warningDim
            case "excluded":
                return Theme.textDisabled
            }
            return Theme.textSecondary
        }

        // The row's own accessible role is deliberately absent. Setting one would put
        // a composed "title: verdict. reason" string on the container while its
        // four child Texts stay in the tree too, and the result is either a double
        // announcement or — if a reader decides to drop children of a labelled
        // container — silence. Left alone, the children are announced in reading
        // order, which is already exactly that sentence: title, verdict, reason,
        // evidence.
        implicitHeight: body.implicitHeight
                         + (showDivider ? Theme.spacingSmall : 0)

        ColumnLayout {
            id: body
            width: parent.width
            spacing: Theme.spacingTiny

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                GateStatusMarker {
                    status: entry.entryData.status
                }

                Text {
                    Layout.fillWidth: true
                    // fontBody, not fontBodyStrong: every row in the table is a
                    // normal state, so no title gets promoted above another.
                    text: entry.surfaceTitle
                    color: Theme.textPrimary
                    font: Theme.fontBody
                    elide: Text.ElideRight
                }

                Text {
                    text: entry.statusText
                    color: entry.statusColor
                    font: Theme.fontCaptionStrong
                }
            }

            // Indented to the title, not to the marker, so the sentence reads as
            // belonging to the surface above it rather than to the shape.
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.iconSmall + Theme.spacingSmall
                text: entry.reasonText
                color: Theme.textPrimaryVariant
                font: Theme.fontCaption
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.iconSmall + Theme.spacingSmall
                text: entry.metaText
                color: Theme.textDisabled
                font: Theme.fontTiny
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.iconSmall + Theme.spacingSmall
                Layout.topMargin: Theme.spacingTiny
                visible: entry.notesVisible && entry.noteText.length > 0
                text: entry.noteText
                color: Theme.textDisabled
                font: Theme.fontTiny
                wrapMode: Text.WordWrap
            }
        }

        // Anchored to the row's bottom, which the implicitHeight above pads by
        // spacingSmall — so the rule sits in the gap between two rows rather than
        // welded to the last line of text.
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.border
            visible: entry.showDivider
        }
    }

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    ColumnLayout {
        id: pageLayout
        x: Theme.spacingLarge
        y: Theme.spacingLarge
        width: root.width - Theme.spacingLarge * 2
        spacing: Theme.spacingMedium

        Text {
            text: "Account"
            color: Theme.textPrimary
            font: Theme.fontHeading
        }

        Text {
            Layout.fillWidth: true
            text: "Connect Suno for your library, generation history, downloads and credit balance."
            color: Theme.textSecondary
            font: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        AccountSessionCard {
            loginState: root.googleLoginState
            googleLoginAvailable: root.googleLoginAvailable
            cancelAvailable: root.cancelGoogleLoginAvailable
            signingOut: root.signingOut
            onSignInRequested: root.beginGoogleSignIn()
            onCancelRequested: root.cancelGoogleSignIn()
            onSignOutRequested: root.signOutSuno()
        }

        Text {
            Layout.fillWidth: true
            visible: root.visibleFeedback.length > 0
            text: root.visibleFeedback
            color: root.visibleFeedbackIsError ? Theme.error : Theme.success
            font: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        // ── Download destination ────────────────────
        // SettingsBridge.sunoDownloadPath is READ+WRITE and already persisted to
        // [suno] download_path, but it had no QML reference at all, so nobody
        // could see or change where a save lands. SunoDownloader::getDownloadDir
        // reads this on every resolve, so a change takes effect on the next save
        // without a restart — which is why there is no "applies next start"
        // notice here.
        SectionHeader {
            text: "Downloads"
        }

        Text {
            Layout.fillWidth: true
            text: "Where saved clips are written. Leave empty to use your system's music folder."
            color: Theme.textSecondary
            font: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            AppTextField {
                id: downloadPathField
                Layout.fillWidth: true
                // Empty is a real value meaning "system music folder", not an
                // unset field, so the placeholder states it rather than the
                // field pretending to be blank.
                placeholderText: "System music folder"
                color: Theme.textPrimary
                font: Theme.fontCaption
                text: SettingsBridge.sunoDownloadPath
                selectByMouse: true
                padding: Theme.spacingSmall
                // onTextEdited, not onTextChanged: the binding above writes
                // `text` too, and this must not push that same value back into
                // the bridge (which debounces an autosave on every write).
                onTextEdited: SettingsBridge.sunoDownloadPath = text
                Accessible.name: "Suno download folder"
                ToolTip.visible: hovered
                ToolTip.text: text.length > 0 ? text : "System music folder"
                ToolTip.delay: 400
            }

            AppButton {
                text: "Browse…"
                onClicked: {
                    if (root.downloadFolder.length > 0)
                        folderDialog.currentFolder =
                                "file://" + encodeURI(root.downloadFolder)
                    folderDialog.open()
                }
                Accessible.name: "Choose a download folder"
            }

            AppButton {
                // Honesty gate: with an empty path there is no directory this app
                // knows about, so the button is absent rather than opening a
                // guessed one.
                visible: root.downloadFolder.length > 0
                text: "Show folder"
                flat: true
                onClicked: Qt.openUrlExternally(root.localFileUrl(root.downloadFolder))
                ToolTip.visible: hovered
                ToolTip.text: root.downloadFolder
                ToolTip.delay: 300
                Accessible.name: "Show " + root.downloadFolder + " in the file manager"
            }
        }

        // The credential paste sits below the download destination rather than
        // above it: the destination is a normal setting you may want to look at
        // without opening the credential disclosure at all.
        AppButton {
            Layout.fillWidth: true
            Layout.topMargin: root.visibleFeedback.length > 0 ? Theme.spacingSmall : 0
            text: root.manualExpanded
                  ? "▾ Paste session cookie header instead"
                  : "▸ Paste session cookie header instead"
            flat: true
            implicitHeight: Theme.buttonHeightLarge
            buttonRadius: Theme.radiusMedium
            onClicked: root.manualExpanded = !root.manualExpanded
            Accessible.name: "Paste session cookie header instead"
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.manualExpanded
            Layout.preferredHeight: manualLayout.implicitHeight + Theme.spacingMedium * 2
            color: Theme.backgroundAlt
            radius: Theme.radiusLarge
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: manualLayout
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                SunoSettings {
                    Layout.fillWidth: true
                }
            }
        }

        // ── Capabilities ──────────────────────────────
        // Why this is on the Account page rather than in its own rail entry: it
        // reads the same session data this page already shows, it is read-only,
        // and its entire job is answering "why is that missing" for the account
        // that is signed in here. It goes below the credential disclosure so the
        // sign-in path stays above the fold — nothing about the table is needed
        // to connect, and the table is the tall one.
        //
        // The framing below is load-bearing. Most of these surfaces are off, and
        // that is the designed answer rather than a fault: this client refuses to
        // build on an uncaptured route, and refuses money-moving and
        // consent-writing routes by product decision. Presented as a wall of
        // warnings it would misdescribe the project's position and read as
        // broken software, so the counts lead, the verdict word carries the
        // state, and the reason carries the cause.
        SectionHeader {
            text: "Capabilities"
        }

        Text {
            Layout.fillWidth: true
            text: "Every Suno surface this client knows about, and the reason it is or is not available to this account. Most of these are closed on purpose."
            color: Theme.textSecondary
            font: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: root.gateModel.counts.total > 0
            // The separator is conditional rather than assumed: a status this page
            // has never heard of would contribute to `total` but to none of the
            // five counts, and "27 surfaces  ·  " with nothing after it is the kind
            // of small wrongness that makes a reader distrust the rest of the line.
            text: root.gateModel.counts.total + " surfaces"
                  + (root.gateModel.summary.length > 0
                     ? "  ·  " + root.gateModel.summary : "")
            color: Theme.textPrimaryVariant
            font: Theme.fontCaptionStrong
            wrapMode: Text.WordWrap
        }

        // Signed out is not the empty state: the table is still full, it is just
        // answering with no session flag map. Saying so is what stops a signed-out
        // reader concluding their account is missing everything.
        Text {
            Layout.fillWidth: true
            visible: root.gateModel.counts.total > 0 && !SunoBridge.isAuthenticated
            text: "Suno is not signed in, so there is no session flag map to read. Surfaces that need a server flag report as not enabled until an account is connected."
            color: Theme.textPrimaryVariant
            font: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        // Empty means "there is nothing to report", not "everything is
        // unavailable" — the bridge hands back no rows at all when no controller
        // is attached. The distinction is stated rather than implied.
        Rectangle {
            Layout.fillWidth: true
            visible: root.gateModel.counts.total === 0
            Layout.preferredHeight: emptyLayout.implicitHeight + Theme.spacingMedium * 2
            color: Theme.backgroundAlt
            radius: Theme.radiusLarge
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: emptyLayout
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Text {
                    Layout.fillWidth: true
                    text: "No capability data"
                    color: Theme.textPrimary
                    font: Theme.fontSubtitle
                }

                Text {
                    Layout.fillWidth: true
                    text: "This table is read from the Suno gate resolver, and there is no Suno controller attached to this build. It does not mean the surfaces are unavailable."
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
            }
        }

        AppButton {
            Layout.topMargin: Theme.spacingSmall
            visible: root.gateModel.counts.total > 0
            text: root.showGateNotes
                  ? "▾ Hide the evidence behind each verdict"
                  : "▸ Show the evidence behind each verdict"
            flat: true
            onClicked: root.showGateNotes = !root.showGateNotes
            Accessible.name: root.showGateNotes
                           ? "Hide the evidence behind each verdict"
                           : "Show the evidence behind each verdict"
            ToolTip.visible: hovered
            ToolTip.text: "Why each surface carries the evidence grade it does, and what a capture would have to add."
            ToolTip.delay: 400
        }

        Repeater {
            model: root.gateModel.flat

            delegate: Item {
                id: sectionItem

                required property var modelData

                // An area header is the start of a new group, so it gets extra
                // air above it rather than the page's uniform 16 px.
                Layout.fillWidth: true
                Layout.topMargin: modelData.kind === "header" ? Theme.spacingSmall : 0
                implicitHeight: modelData.kind === "header"
                                 ? headerLayout.implicitHeight
                                 : surfaceRow.implicitHeight

                ColumnLayout {
                    id: headerLayout

                    width: parent.width
                    spacing: Theme.spacingTiny
                    visible: sectionItem.modelData.kind === "header"

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            Layout.fillWidth: true
                            text: sectionItem.modelData.label
                            color: Theme.textPrimary
                            font: Theme.fontSubtitle
                            elide: Text.ElideRight
                        }

                        Text {
                            text: sectionItem.modelData.count === 1
                                  ? "1 surface" : sectionItem.modelData.count + " surfaces"
                            color: Theme.textDisabled
                            font: Theme.fontTiny
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.border
                    }
                }

                GateStatusRow {
                    id: surfaceRow

                    width: parent.width
                    visible: sectionItem.modelData.kind === "row"
                    entryData: sectionItem.modelData.data
                    statusText: sectionItem.modelData.statusText
                    notesVisible: sectionItem.modelData.notes
                    showDivider: sectionItem.modelData.divider
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Choose where saved clips are written"

        // currentFolder is set imperatively on open rather than bound: with an
        // empty sunoDownloadPath there is no folder to start from, and a bound
        // "" is an invalid URL the dialog would have to reject every time it
        // opened. So the dialog starts where Qt would have started anyway, and
        // only inherits an existing destination when there is one.
        onAccepted: {
            const path = root.toLocalPath(selectedFolder)
            if (path.length > 0)
                SettingsBridge.sunoDownloadPath = path
        }
    }

    Connections {
        target: SunoBridge
        ignoreUnknownSignals: true

        function onAuthenticationChanged() {
            if (SunoBridge.isAuthenticated) {
                root.signingOut = false
            } else if (root.signingOut) {
                root.signingOut = false
                root.showFeedback("Suno session signed out.", false)
            }
        }

        function onAuthenticationFailed() {
            root.signingOut = false
        }
    }
}
