/**
 * @file VisualizerSettings.qml
 * @brief Settings section: visualizer engine (FPS, mesh, durations,
 *        beat sensitivity, shuffle, aspect correction)
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Visualizer Engine"
    }

    SettingSpinRow {
        label: "FPS Limit"
        from: 15; to: 240; stepSize: 15
        value: SettingsBridge.visualizerFps
        onValueMoved: (val) => SettingsBridge.visualizerFps = val
    }

    SettingSpinRow {
        label: "Mesh Complexity (X)"
        from: 16; to: 512; stepSize: 8
        value: SettingsBridge.visualizerMeshX
        onValueMoved: (val) => SettingsBridge.visualizerMeshX = val
    }

    SettingSpinRow {
        label: "Mesh Complexity (Y)"
        from: 8; to: 512; stepSize: 8
        value: SettingsBridge.visualizerMeshY
        onValueMoved: (val) => SettingsBridge.visualizerMeshY = val
    }

    SettingSpinRow {
        label: "Preset Duration (s)"
        from: 5; to: 300; stepSize: 5
        value: SettingsBridge.visualizerPresetDuration
        onValueMoved: (val) => SettingsBridge.visualizerPresetDuration = val
    }

    SettingSpinRow {
        label: "Smooth Preset Duration (s)"
        from: 0; to: 30; stepSize: 1
        value: SettingsBridge.visualizerSmoothPresetDuration
        onValueMoved: (val) => SettingsBridge.visualizerSmoothPresetDuration = val
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Beat Sensitivity"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        Slider {
            from: 0.1; to: 3.0
            value: SettingsBridge.visualizerBeatSensitivity
            onMoved: SettingsBridge.visualizerBeatSensitivity = value
            Layout.preferredWidth: 150
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Shuffle Presets"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        AppSwitch {
            checked: SettingsBridge.visualizerShufflePresets
            onToggled: SettingsBridge.visualizerShufflePresets = checked
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Aspect Correction"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        AppSwitch {
            checked: SettingsBridge.visualizerAspectCorrection
            onToggled: SettingsBridge.visualizerAspectCorrection = checked
        }
    }
}
