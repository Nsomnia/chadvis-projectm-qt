/**
 * @file RecordingSettings.qml
 * @brief Settings section: video recorder (resolution, FPS, CRF,
 *        encoder preset, audio codec, bitrate)
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Recording"
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Video Resolution"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        AppComboBox {
            id: resolutionCombo
            model: ["720p", "1080p", "1440p", "4K"]
            currentIndex: {
                var w = SettingsBridge.recorderWidth
                var h = SettingsBridge.recorderHeight
                if (w === 3840 && h === 2160) return 3
                if (w === 2560 && h === 1440) return 2
                if (w === 1920 && h === 1080) return 1
                if (w === 1280 && h === 720) return 0
                return 1
            }
            onActivated: {
                switch (currentIndex) {
                case 0: SettingsBridge.recorderWidth = 1280; SettingsBridge.recorderHeight = 720; break
                case 1: SettingsBridge.recorderWidth = 1920; SettingsBridge.recorderHeight = 1080; break
                case 2: SettingsBridge.recorderWidth = 2560; SettingsBridge.recorderHeight = 1440; break
                case 3: SettingsBridge.recorderWidth = 3840; SettingsBridge.recorderHeight = 2160; break
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Resolution"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        Text {
            color: Theme.textPrimary
            font: Theme.fontCaption
            text: SettingsBridge.recorderWidth + " × " + SettingsBridge.recorderHeight
        }
    }

    SettingSpinRow {
        label: "FPS"
        from: 10; to: 120; stepSize: 5
        value: SettingsBridge.recorderFps
        onValueMoved: (val) => SettingsBridge.recorderFps = val
    }

    SettingSpinRow {
        label: "CRF (Quality)"
        from: 0; to: 51
        value: SettingsBridge.recorderCrf
        onValueMoved: (val) => SettingsBridge.recorderCrf = val
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Encoder Preset"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        AppComboBox {
            model: ["ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"]
            currentIndex: model.indexOf(SettingsBridge.recorderPreset)
            onActivated: SettingsBridge.recorderPreset = currentText
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Audio Codec"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        AppComboBox {
            id: audioCodecCombo
            model: ["aac", "opus", "mp3", "flac"]
            currentIndex: model.indexOf(SettingsBridge.recorderAudioCodec)
            onActivated: SettingsBridge.recorderAudioCodec = currentText
        }
    }

    SettingSpinRow {
        label: "Audio Bitrate (kbps)"
        from: 64; to: 640; stepSize: 32
        value: SettingsBridge.recorderAudioBitrate
        onValueMoved: (val) => SettingsBridge.recorderAudioBitrate = val
    }
}
