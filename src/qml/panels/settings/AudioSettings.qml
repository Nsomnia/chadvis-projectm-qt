/**
 * @file AudioSettings.qml
 * @brief Settings section: audio engine (sample rate, device, buffer)
 *
 * Every control here is wired to the engine. Before this was true the device
 * field was a hardcoded AppTextField reading "default" with no binding and no
 * handler, and the buffer row was labelled "(ms)" over a value that is a sample
 * count and that nothing read.
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
            // applyAudioConfig() is required, not decorative: audioSampleRate is
            // generated from the SettingsBridgeSettings.inc table, so its setter
            // cannot push to the engine by itself.
            onActivated: {
                SettingsBridge.audioSampleRate = parseInt(currentText)
                SettingsBridge.applyAudioConfig()
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Audio Device"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        AppComboBox {
            Layout.preferredWidth: 220
            model: SettingsBridge.audioDevices
            enabled: SettingsBridge.audioDevicesAvailable
            currentIndex: {
                // audioDevice holds the persisted value, which for the system
                // default is the sentinel "default" rather than the row's display
                // label. index 0 IS that row, so falling back to it is the
                // correct answer and not a coincidence.
                var idx = SettingsBridge.audioDevices.indexOf(SettingsBridge.audioDevice)
                return idx >= 0 ? idx : 0
            }
            onActivated: (index) => SettingsBridge.audioDevice = SettingsBridge.audioDevices[index]
        }
    }

    // Always visible. A device control that cannot select a device, or that only
    // takes effect on the next launch, has to say which it is.
    Text {
        Layout.fillWidth: true
        text: SettingsBridge.audioDeviceNotice
        color: Theme.textDisabled
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    SettingSpinRow {
        label: "Audio Buffer (samples)"
        // The stored value is an interleaved sample count, so the range is the
        // conversion window's own range. The engine clamps to it, and the status
        // line below reports the window it is really using.
        from: 4096; to: 16384; stepSize: 256
        value: SettingsBridge.audioBufferSize
        onValueMoved: (val) => {
            SettingsBridge.audioBufferSize = val
            SettingsBridge.applyAudioConfig()
        }
    }

    Text {
        Layout.fillWidth: true
        text: "Window in interleaved samples, clamped to 4096–16384. " +
              "Qt 6 exposes no device output-period control, so this is the " +
              "engine's conversion window, not the device's buffer."
        color: Theme.textDisabled
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    // Live output status. The sink rate is the platform's choice, so this is the
    // only place the effect of the sample-rate preference becomes visible.
    Text {
        Layout.fillWidth: true
        visible: SettingsBridge.audioOutputStatus.length > 0
        text: SettingsBridge.audioOutputStatus
        color: Theme.textDisabled
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        visible: SettingsBridge.audioError.length > 0
        text: SettingsBridge.audioError
        color: Theme.error
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }
}
