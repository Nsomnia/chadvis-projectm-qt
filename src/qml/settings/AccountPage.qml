import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../components"
import "../panels/settings"

Flickable {
    id: root

    property bool authenticationPending: false
    property bool signingOut: false
    property bool manualExpanded: false
    property string feedbackMessage: ""
    property bool feedbackIsError: false

    readonly property bool signedIn: SunoBridge.isAuthenticated
    readonly property bool authenticating: authenticationPending && !signedIn
    readonly property var bridgeApi: SunoBridge

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
        if (!bridgeSupports("beginGoogleSignIn")) {
            manualExpanded = true
            showFeedback("Browser sign-in is unavailable in this build. Paste a session token below.", true)
            return
        }

        authenticationPending = true
        authenticationWatchdog.restart()
        bridgeApi["beginGoogleSignIn"]()
    }

    function cancelGoogleSignIn() {
        if (bridgeSupports("cancelGoogleSignIn"))
            bridgeApi["cancelGoogleSignIn"]()

        authenticationPending = false
        authenticationWatchdog.stop()
        showFeedback("Browser sign-in cancelled.", false)
    }

    function signOutSuno() {
        feedbackMessage = ""
        if (!bridgeSupports("signOutSuno")) {
            showFeedback("Sign out is unavailable in this build.", true)
            return
        }

        signingOut = true
        bridgeApi["signOutSuno"]()
    }

    function stopAuthenticationWatchdog() {
        authenticationWatchdog.stop()
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
            signedIn: root.signedIn
            authenticating: root.authenticating
            signingOut: root.signingOut
            onSignInRequested: root.beginGoogleSignIn()
            onCancelRequested: root.cancelGoogleSignIn()
            onSignOutRequested: root.signOutSuno()
        }

        Text {
            Layout.fillWidth: true
            visible: root.feedbackMessage.length > 0
            text: root.feedbackMessage
            color: root.feedbackIsError ? Theme.error : Theme.success
            font: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        AppButton {
            Layout.fillWidth: true
            Layout.topMargin: root.feedbackMessage.length > 0 ? Theme.spacingSmall : 0
            text: root.manualExpanded
                  ? "▾ Paste session cookie or token instead"
                  : "▸ Paste session cookie or token instead"
            flat: true
            implicitHeight: Theme.buttonHeightLarge
            buttonRadius: Theme.radiusMedium
            onClicked: root.manualExpanded = !root.manualExpanded
            Accessible.name: "Paste session cookie or token instead"
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

                Text {
                    Layout.fillWidth: true
                    text: "Use a Suno session cookie or bearer token when browser sign-in is unavailable."
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }

                SunoSettings {
                    Layout.fillWidth: true
                }
            }
        }
    }

    Connections {
        target: SunoBridge
        ignoreUnknownSignals: true

        function onAuthenticationChanged() {
            if (SunoBridge.isAuthenticated) {
                root.authenticationPending = false
                root.signingOut = false
                root.stopAuthenticationWatchdog()
                root.showFeedback("Suno session connected.", false)
            } else if (root.signingOut) {
                root.signingOut = false
                root.showFeedback("Suno session signed out.", false)
            }
        }

        function onAuthenticationFailed(message) {
            root.authenticationPending = false
            root.signingOut = false
            root.stopAuthenticationWatchdog()
            root.showFeedback(message || "Suno sign-in could not be completed.", true)
        }
    }

    Timer {
        id: authenticationWatchdog
        interval: 120000
        repeat: false
        onTriggered: {
            root.authenticationPending = false
            root.showFeedback("Browser sign-in timed out. Retry or paste a session token below.", true)
        }
    }
}
