import QtQuick
import QtQuick.Controls
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
