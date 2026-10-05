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
