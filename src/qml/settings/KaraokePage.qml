import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../components"

Flickable {
    id: root

    contentHeight: pageLayout.implicitHeight + Theme.spacingXL * 2
    clip: true

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
            text: "Karaoke"
            color: Theme.textPrimary
            font: Theme.fontHeading
        }

        Text {
            Layout.fillWidth: true
            text: "Shape how synchronized lyrics sit and read over the Video canvas."
            color: Theme.textSecondary
            font: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        KaraokeSettings {
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: captionLayout.implicitHeight + Theme.spacingMedium * 2
            color: Theme.backgroundAlt
            radius: Theme.radiusLarge
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: captionLayout
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingTiny

                        Text {
                            text: "Caption track"
                            color: Theme.accent
                            font: Theme.fontSubtitle
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Show synchronized lyrics above the projectM canvas."
                            color: Theme.textSecondary
                            font: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                    }

                    AppSwitch {
                        checked: SettingsBridge.karaokeEnabled
                        onToggled: SettingsBridge.karaokeEnabled = checked
                    }
                }
            }
        }
    }
}
