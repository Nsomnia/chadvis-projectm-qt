/**
 * @file ComingSoonPage.qml
 * @brief Tasteful placeholder for roadmap views (Canvas / Studio / Automation)
 *
 * Sets product direction without fake features: ghost glyph, one-line
 * positioning statement and the concrete roadmap bullets that will land
 * here (mirrors docs/PIVOT_PLAN.md phases). Deliberately quiet — generous
 * negative space, hairline glass ring, no buttons that pretend to work.
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import ChadVis

Item {
    id: root

    property string pageTitle: ""
    property string blurb: ""
    property string glyph: "◇"
    property var milestones: []

    Rectangle {
        anchors.centerIn: parent
        width: 120
        height: 120
        radius: Theme.radiusRound
        color: Theme.glassHighlight
        border.width: 1
        border.color: Theme.glassBorder

        // Slow breathing glow — alive but patient
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { to: 0.55; duration: 2600; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 2600; easing.type: Easing.InOutSine }
        }

        Text {
            anchors.centerIn: parent
            text: root.glyph
            color: Theme.accent
            font.pixelSize: 44
        }
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.verticalCenter
        anchors.topMargin: Theme.spacingXL
        spacing: Theme.spacingSmall
        width: Math.min(420, parent.width - Theme.spacingXL * 2)

        Text {
            text: root.pageTitle
            color: Theme.textPrimary
            font: Theme.fontHeading
            Layout.alignment: Qt.AlignHCenter
        }

        Item { Layout.preferredHeight: Theme.spacingTiny }

        Text {
            text: root.blurb
            color: Theme.textPrimaryVariant
            font: Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Item { Layout.preferredHeight: Theme.spacingMedium }

        ColumnLayout {
            spacing: Theme.spacingSmall
            Layout.alignment: Qt.AlignHCenter

            Repeater {
                model: root.milestones

                delegate: RowLayout {
                    spacing: Theme.spacingSmall
                    Layout.alignment: Qt.AlignHCenter

                    Rectangle {
                        width: 14
                        height: 2
                        radius: 1
                        color: Theme.accentDark
                    }

                    Text {
                        text: modelData
                        color: Theme.textSecondary
                        font: Theme.fontCaption
                    }
                }
            }
        }

        Item { Layout.preferredHeight: Theme.spacingLarge }

        Text {
            text: "COMING ONLINE SOON"
            color: Theme.warningDim
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaptionStrong.pixelSize
            font.weight: Font.DemiBold
            font.letterSpacing: 3
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
