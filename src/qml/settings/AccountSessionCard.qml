import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../components"

Rectangle {
    id: root

    required property string loginState
    required property bool googleLoginAvailable
    required property bool signingOut

    readonly property bool authenticated: SunoBridge.isAuthenticated
    readonly property bool browserOpen: loginState === "browserOpen"

    signal signInRequested()
    signal signOutRequested()

    readonly property string displayName: SunoBridge.userName.length > 0
                                             ? SunoBridge.userName : "Suno member"
    readonly property string displayPlan: SunoBridge.planName.length > 0
                                           ? SunoBridge.planName : "Suno"

    Layout.fillWidth: true
    Layout.preferredHeight: 292
    color: Theme.backgroundAlt
    radius: Theme.radiusXL
    border.width: 1
    border.color: root.authenticated ? Theme.successDim : Theme.border

    StackLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        // T0181: two real states plus the signed-in one. The old
        // "callbackReceived" page waited for a code that Clerk never sends to
        // this app; index 1 is now "you are in your browser, here is what to do
        // next", which is the only thing left for the UI to communicate.
        currentIndex: root.authenticated ? 2 : (root.browserOpen ? 1 : 0)

        ColumnLayout {
            spacing: Theme.spacingMedium

            Item { Layout.fillHeight: true }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "ChadVis"
                color: Theme.textPrimary
                font: Theme.fontDisplay
            }

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                text: "Your Suno desktop, ready when you are."
                color: Theme.textSecondary
                font: Theme.fontBody
                horizontalAlignment: Text.AlignHCenter
            }

            AppButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 220
                Layout.preferredHeight: Theme.buttonHeightLarge
                // T0181: the button opens the SYSTEM browser, not a loopback
                // listener. Clerk completes the Google exchange server-side, so
                // this application never receives an authorization code and
                // cannot wait for one -- opening the browser is the whole flow.
                text: "Sign in with Google"
                highlighted: true
                enabled: root.googleLoginAvailable
                buttonRadius: Theme.radiusMedium
                onClicked: root.signInRequested()
                ToolTip.visible: hovered && !enabled
                ToolTip.text: "Secure credential storage is unavailable, so a session cannot be stored. Import a session cookie or bearer token below."
                ToolTip.delay: 350
                Accessible.name: "Sign in with Google"
            }

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                text: "Your browser opens for Google, then ChadVis receives the Suno session."
                color: Theme.textSecondary
                font: Theme.fontCaption
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Item { Layout.fillHeight: true }
        }

        ColumnLayout {
            spacing: Theme.spacingMedium

            Item { Layout.fillHeight: true }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 48
                implicitHeight: 48
                running: root.browserOpen
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Finish in your browser"
                color: Theme.textPrimary
                font: Theme.fontSubtitle
            }

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "ChadVis opened Suno in your browser. Sign in with Google "
                      + "there, then paste your session cookie below."
                color: Theme.textSecondary
                font: Theme.fontCaption
            }

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                text: "Finish Google sign-in there; this window will update when Suno returns the session."
                color: Theme.textSecondary
                font: Theme.fontBody
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            // No Cancel button (T0181). There is nothing to cancel: the browser
            // is the user's own, this app cannot close it, and the flow it is in
            // has no client-side state left to abandon.

            Item { Layout.fillHeight: true }
        }

        ColumnLayout {
            spacing: Theme.spacingMedium

            Item { Layout.fillHeight: true }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.displayName
                color: Theme.textPrimary
                font: Theme.fontTitle
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Session active"
                color: Theme.success
                font: Theme.fontCaptionStrong
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: 480
                spacing: Theme.spacingSmall

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 74
                    color: Theme.surface
                    radius: Theme.radiusMedium
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 0

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: root.displayPlan
                            color: Theme.textPrimary
                            font: Theme.fontSubtitle
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Text {
                            text: "Plan"
                            color: Theme.textDisabled
                            font: Theme.fontTiny
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 74
                    color: Theme.surface
                    radius: Theme.radiusMedium
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 0

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: SunoBridge.credits
                            color: Theme.accent
                            font: Theme.fontSubtitle
                        }

                        Text {
                            text: "Credits"
                            color: Theme.textDisabled
                            font: Theme.fontTiny
                        }
                    }
                }
            }

            AppButton {
                Layout.alignment: Qt.AlignHCenter
                text: root.signingOut ? "Signing out…" : "Sign out"
                enabled: !root.signingOut
                onClicked: root.signOutRequested()
            }

            Item { Layout.fillHeight: true }
        }
    }

    Timer {
        id: credentialReloadDebounce
        interval: 500
        onTriggered: SunoBridge.refreshAccount()
    }

    Connections {
        target: SettingsBridge
        function onSunoTokenChanged() {
            credentialReloadDebounce.restart()
        }
    }

    Component.onCompleted: SunoBridge.refreshAccount()
}
