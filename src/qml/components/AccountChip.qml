/**
 * @file AccountChip.qml
 * @brief Suno account pill: user name, plan badge, credit balance
 *
 * Reads the read-only account snapshot off SunoBridge (populated after
 * auth reaches ActiveValid). Tolerant of empty/zero state: renders a
 * muted "Sign in via Settings" hint until data arrives.
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis

Rectangle {
    id: root

    readonly property bool hasAccount: SunoBridge.userName !== ""

    implicitWidth: chipRow.implicitWidth + Theme.spacingMedium * 2
    implicitHeight: 28
    radius: Theme.radiusRound

    color: Theme.glassBackground
    border.width: 1
    border.color: root.hasAccount ? Theme.glassBorder : Theme.border

    scale: chipMouse.pressed ? 0.97 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.durationInstant } }

    RowLayout {
        id: chipRow
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        // Credit glyph — accent disc with bolt-ish "¤"? Keep to a spark dot.
        Rectangle {
            visible: root.hasAccount
            width: 8
            height: 8
            radius: 4
            color: Theme.accent
        }

        Text {
            visible: root.hasAccount
            text: SunoBridge.userName
            color: Theme.textPrimary
            font: Theme.fontCaptionStrong
            elide: Text.ElideRight
            Layout.maximumWidth: 120
        }

        // Plan badge (e.g. PRO / PREMIER)
        Rectangle {
            visible: root.hasAccount && SunoBridge.planName !== ""
            implicitWidth: planLabel.implicitWidth + 10
            implicitHeight: 16
            radius: Theme.radiusRound
            color: Theme.glassHighlight
            border.width: 1
            border.color: Theme.glassBorder

            Text {
                id: planLabel
                anchors.centerIn: parent
                text: SunoBridge.planName.toUpperCase()
                color: Theme.accentLight
                font: Theme.fontTiny
            }
        }

        Text {
            visible: root.hasAccount
            text: SunoBridge.credits + " cr"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        Text {
            visible: !root.hasAccount
            text: "Suno · sign in via Settings"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }
    }

    MouseArea {
        id: chipMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.navigateRequested()
    }

    signal navigateRequested()

    ToolTip.visible: chipMouse.containsMouse && root.hasAccount
    ToolTip.text: SunoBridge.planName + " plan · " + SunoBridge.credits + " credits remaining"
    ToolTip.delay: 400
}
