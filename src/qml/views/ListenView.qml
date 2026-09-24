import QtQuick
import QtQuick.Layouts
import ChadVis
import "../components"
import "../panels"

Item {
    id: root

    readonly property bool compact: width < 760

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingMedium

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compact ? 96 : 124
            color: Theme.backgroundAlt
            radius: Theme.radiusLarge
            border.width: 1
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingMedium

                Rectangle {
                    Layout.preferredWidth: root.compact ? 48 : 64
                    Layout.preferredHeight: root.compact ? 48 : 64
                    color: Theme.surfaceRaised
                    radius: Theme.radiusMedium
                    border.width: 1
                    border.color: Theme.glassBorder

                    Text {
                        anchors.centerIn: parent
                        text: "♪"
                        color: Theme.accent
                        font: Theme.fontHeading
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingTiny

                    Text {
                        text: "NOW PLAYING"
                        color: Theme.accent
                        font: Theme.fontTiny
                    }

                    Text {
                        Layout.fillWidth: true
                        text: AudioBridge.currentTrack.title || "No track selected"
                        color: Theme.textPrimary
                        font: root.compact ? Theme.fontSubtitle : Theme.fontTitle
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: AudioBridge.currentTrack.artist || "Add audio from the queue or open a file"
                        color: Theme.textSecondary
                        font: Theme.fontBody
                        elide: Text.ElideRight
                    }
                }

                PulseIndicator {
                    active: AudioBridge.isPlaying
                    baseColor: Theme.success
                    size: Theme.iconMedium
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: root.compact ? 1 : 2
            columnSpacing: Theme.spacingMedium
            rowSpacing: Theme.spacingMedium

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: root.compact ? 1 : 3
                Layout.minimumHeight: 120
                color: Theme.backgroundAlt
                radius: Theme.radiusLarge
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Text {
                        text: "Queue"
                        color: Theme.accent
                        font: Theme.fontSubtitle
                    }

                    PlaylistPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: root.compact ? 1 : 2
                Layout.minimumHeight: 120
                color: Theme.backgroundAlt
                radius: Theme.radiusLarge
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Text {
                        text: "Lyrics"
                        color: Theme.accent
                        font: Theme.fontSubtitle
                    }

                    LyricsPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }
        }

        TransportBar {
            Layout.fillWidth: true
            compact: root.compact
        }
    }
}
