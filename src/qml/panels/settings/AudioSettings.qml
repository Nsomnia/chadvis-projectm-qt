/**
 * @file AudioSettings.qml
 * @brief Settings section: audio engine (sample rate, device, buffer)
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Audio Engine"
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Sample Rate"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        AppComboBox {
            model: [22050, 44100, 48000, 96000]
            currentIndex: {
                var rates = [22050, 44100, 48000, 96000]
                var idx = rates.indexOf(SettingsBridge.audioSampleRate)
                return idx >= 0 ? idx : 1
            }
            onActivated: SettingsBridge.audioSampleRate = parseInt(currentText)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Audio Device"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        AppTextField {
            id: audioDeviceInput
            Layout.fillWidth: true
            text: "default"
            placeholderText: "Audio device name..."
        }
    }

    SettingSpinRow {
        label: "Audio Buffer (ms)"
        from: 10; to: 1000; stepSize: 10
        value: SettingsBridge.audioBufferSize
        onValueMoved: (val) => SettingsBridge.audioBufferSize = val
    }
}
