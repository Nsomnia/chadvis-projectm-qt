/**
 * @file VisualizerSettings.qml
 * @brief Settings section: visualizer engine (FPS, mesh, durations,
 *        beat sensitivity, shuffle, aspect correction)
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    id: root
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    // FolderDialog hands back a url; strip the scheme and percent-decode, and
    // drop the extra slash before a Windows drive letter. Same helper as
    // AccountPage's toLocalPath() and RecordingPanel's toLocalFilePath().
    function toLocalPath(url) {
        const s = url.toString()
        if (!s.startsWith("file://"))
            return ""
        let path = decodeURIComponent(s.slice("file://".length))
        if (/^\/[A-Za-z]:\//.test(path))
            path = path.slice(1)
        return path
    }

    SectionHeader {
        text: "Preset Library"
    }

    // The library is what the renderer draws from: with no pack there is exactly
    // one picture (projectM's idle preset), so this row is the difference
    // between a visualizer and a screensaver. The folder is shown rather than
    // implied because the default is a decision the build made, not one the user
    // can guess from a count.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        Text {
            Layout.fillWidth: true
            text: PresetBridge.folder
            color: Theme.textSecondary
            font: Theme.fontCaption
            elide: Text.ElideMiddle
            ToolTip.visible: folderHover.hovered && PresetBridge.folder.length > 0
            ToolTip.text: PresetBridge.folder
            ToolTip.delay: 300
            HoverHandler { id: folderHover }
        }

        AppButton {
            text: "Choose…"
            onClicked: presetFolderDialog.open()
        }

        AppButton {
            text: "Rescan"
            onClicked: PresetBridge.rescan()
        }
    }

    Text {
        Layout.fillWidth: true
        text: PresetBridge.presetCount === 0
              ? "No presets found in this folder. The canvas can only show projectM's built-in idle preset."
              : PresetBridge.presetCount + (PresetBridge.presetCount === 1 ? " preset" : " presets")
                + " available"
        color: PresetBridge.presetCount === 0 ? Theme.warning : Theme.textSecondary
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    FolderDialog {
        id: presetFolderDialog
        title: "Choose a preset folder"
        // Seeded on open rather than bound: an empty bound "" is an invalid url
        // the dialog would have to reject every time.
        onAccepted: {
            const path = root.toLocalPath(selectedFolder)
            if (path.length > 0)
                PresetBridge.folder = path
        }
    }

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
