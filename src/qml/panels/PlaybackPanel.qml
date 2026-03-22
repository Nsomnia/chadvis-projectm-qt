/**
 * @file PlaybackPanel.qml
 * @brief Compact playback controls for accordion panel
 *
 * Single-responsibility: Audio playback UI in accordion context
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

ColumnLayout {
    id: root

    spacing: Theme.spacingMedium

    // ═══════════════════════════════════════════════════════════
    // NOW PLAYING
    // ═══════════════════════════════════════════════════════════

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        Rectangle {
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            color: Theme.surfaceOverlay
            radius: Theme.radiusSmall

            Text {
                anchors.centerIn: parent
                text: "♪"
                color: Theme.textSecondary
                font.pixelSize: 24
            }

            border.color: Theme.border
            border.width: 1
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: AudioBridge.currentTrack.title || "Not Playing"
                color: Theme.textPrimary
                font: Theme.fontBodyStrong
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Text {
                text: AudioBridge.currentTrack.artist || ""
                color: Theme.textSecondary
                font: Theme.fontCaption
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // TRANSPORT CONTROLS
    // ═══════════════════════════════════════════════════════════

    RowLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        spacing: Theme.spacingSmall

        AppButton {
            icon: "qrc:/icons/prev.svg"
            implicitWidth: 40
            implicitHeight: 40
            radius: Theme.radiusMedium
            onClicked: AudioBridge.previous()
        }

        AppButton {
            icon: AudioBridge.isPlaying ? "qrc:/icons/pause.svg" : "qrc:/icons/play.svg"
            highlighted: AudioBridge.isPlaying
            implicitWidth: 48
            implicitHeight: 48
            radius: Theme.radiusMedium
            onClicked: AudioBridge.togglePlayPause()
        }

        AppButton {
            icon: "qrc:/icons/stop.svg"
            implicitWidth: 40
            implicitHeight: 40
            radius: Theme.radiusMedium
            onClicked: AudioBridge.stop()
        }

        AppButton {
            icon: "qrc:/icons/next.svg"
            implicitWidth: 40
            implicitHeight: 40
            radius: Theme.radiusMedium
            onClicked: AudioBridge.next()
        }
    }

    // ═══════════════════════════════════════════════════════════
    // SEEK BAR
    // ═══════════════════════════════════════════════════════════

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTiny

        AppSlider {
            Layout.fillWidth: true
            from: 0
            to: AudioBridge.duration || 1
            value: AudioBridge.position
            onMoved: AudioBridge.seek(value)
        }

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: Theme.formatTime(AudioBridge.position)
                color: Theme.textSecondary
                font: Theme.fontCaption
            }

            Item { Layout.fillWidth: true }

            Text {
                text: Theme.formatTime(AudioBridge.duration)
                color: Theme.textSecondary
                font: Theme.fontCaption
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // VOLUME CONTROL
    // ═══════════════════════════════════════════════════════════

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        AppButton {
            icon: AudioBridge.volume > 0 ? "qrc:/icons/volume-high.svg" : "qrc:/icons/volume-mute.svg"
            flat: true
            implicitWidth: 32
            implicitHeight: 32
            radius: Theme.radiusSmall
            onClicked: AudioBridge.setVolume(AudioBridge.volume > 0 ? 0 : 0.5)
        }

        AppSlider {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: AudioBridge.volume * 100
            onMoved: AudioBridge.setVolume(value / 100)
        }

        Text {
            text: Math.round(AudioBridge.volume * 100) + "%"
            color: Theme.textSecondary
            font: Theme.fontCaption
            Layout.preferredWidth: 36
            horizontalAlignment: Text.AlignRight
        }
    }
}
