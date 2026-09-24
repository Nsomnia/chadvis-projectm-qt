import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../components"

Rectangle {
    id: root

    required property bool signedIn
    required property bool authenticating
    required property bool signingOut

    signal signInRequested()
    signal cancelRequested()
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
    border.color: signedIn ? Theme.successDim : Theme.border

    StackLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        currentIndex: root.signedIn ? 2 : (root.authenticating ? 1 : 0)

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
                text: "Sign in with Google"
                highlighted: true
                buttonRadius: Theme.radiusMedium
                onClicked: root.signInRequested()
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
                running: root.authenticating
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Waiting for your browser…"
                color: Theme.textPrimary
                font: Theme.fontSubtitle
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

            AppButton {
                Layout.alignment: Qt.AlignHCenter
                text: "Cancel"
                flat: true
                onClicked: root.cancelRequested()
            }

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
}
