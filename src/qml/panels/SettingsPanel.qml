import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import ChadVis
import "../components"

Flickable {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true
    contentHeight: contentLayout.implicitHeight + Theme.spacingXL
    clip: true

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // ═══════════════════════════════════════════════════════════
        // PERFORMANCE PRESETS
        // ═══════════════════════════════════════════════════════════
        Text {
            text: "Performance Presets"
            color: Theme.accent
            font: Theme.fontSubtitle
            Layout.topMargin: Theme.spacingSmall
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Repeater {
                model: ["Performance", "Balanced", "High Fidelity", "Ultra (Chad)"]
                delegate: AppButton {
                    text: modelData
                    Layout.fillWidth: true
                    onClicked: SettingsBridge.setPerformancePreset(modelData)
                }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // APPEARANCE
        // ═══════════════════════════════════════════════════════════
        Text {
            text: "Appearance"
            color: Theme.accent
            font: Theme.fontSubtitle
            Layout.topMargin: Theme.spacingSmall
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMedium
            rowSpacing: Theme.spacingSmall

            Text { text: "Accent"; color: Theme.textSecondary; font: Theme.fontCaption }
            RowLayout {
                Layout.fillWidth: true
                Rectangle { width: 20; height: 20; radius: 4; color: accentInput.text; border.color: Theme.border }
                TextField {
                    id: accentInput
                    Layout.fillWidth: true
                    text: Theme.accent
                    background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: parent.activeFocus ? Theme.accent : Theme.border }
                }
            }

            Text { text: "Background"; color: Theme.textSecondary; font: Theme.fontCaption }
            RowLayout {
                Layout.fillWidth: true
                Rectangle { width: 20; height: 20; radius: 4; color: bgInput.text; border.color: Theme.border }
                TextField {
                    id: bgInput
                    Layout.fillWidth: true
                    text: Theme.background
                    background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: parent.activeFocus ? Theme.accent : Theme.border }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // AUDIO ENGINE
        // ═══════════════════════════════════════════════════════════
        Text {
            text: "Audio Engine"
            color: Theme.accent
            font: Theme.fontSubtitle
            Layout.topMargin: Theme.spacingSmall
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Sample Rate"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            ComboBox {
                model: [22050, 44100, 48000, 96000]
                currentIndex: {
                    var rates = [22050, 44100, 48000, 96000]
                    var idx = rates.indexOf(SettingsBridge.audioSampleRate)
                    return idx >= 0 ? idx : 1
                }
                onActivated: SettingsBridge.audioSampleRate = parseInt(currentText)
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
                contentItem: Text {
                    text: parent.displayText
                    color: Theme.textPrimary
                    font: Theme.fontCaption
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Audio Device"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            TextField {
                id: audioDeviceInput
                Layout.fillWidth: true
                text: "default"
                placeholderText: "Audio device name..."
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: parent.activeFocus ? Theme.accent : Theme.border }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Audio Buffer (ms)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 10; to: 1000; stepSize: 10
                value: SettingsBridge.audioBufferSize
                onValueModified: SettingsBridge.audioBufferSize = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // VISUALIZER ENGINE
        // ═══════════════════════════════════════════════════════════
        Text {
            text: "Visualizer Engine"
            color: Theme.accent
            font: Theme.fontSubtitle
            Layout.topMargin: Theme.spacingSmall
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "FPS Limit"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 15; to: 240; stepSize: 15
                value: SettingsBridge.visualizerFps
                onValueModified: SettingsBridge.visualizerFps = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Mesh Complexity (X)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 16; to: 512; stepSize: 8
                value: SettingsBridge.visualizerMeshX
                onValueModified: SettingsBridge.visualizerMeshX = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Mesh Complexity (Y)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 8; to: 512; stepSize: 8
                value: SettingsBridge.visualizerMeshY
                onValueModified: SettingsBridge.visualizerMeshY = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Preset Duration (s)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                id: presetDurationSpin
                from: 5; to: 300; stepSize: 5
                value: SettingsBridge.visualizerPresetDuration
                onValueModified: SettingsBridge.visualizerPresetDuration = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Smooth Preset Duration (s)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                id: smoothPresetDurationSpin
                from: 0; to: 30; stepSize: 1
                value: SettingsBridge.visualizerSmoothPresetDuration
                onValueModified: SettingsBridge.visualizerSmoothPresetDuration = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
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
            Switch {
                id: shuffleSwitch
                checked: SettingsBridge.visualizerShufflePresets
                onToggled: SettingsBridge.visualizerShufflePresets = checked
                indicator: Rectangle {
                    implicitWidth: 40; implicitHeight: 20
                    x: parent.leftPadding; y: parent.topPadding + (parent.availableHeight - height) / 2
                    radius: 10; color: parent.checked ? Theme.accent : Theme.surfaceOverlay
                    border.color: Theme.border
                    Rectangle {
                        width: 16; height: 16; radius: 8
                        x: parent.checked ? parent.width - width - 2 : 2
                        y: (parent.height - height) / 2
                        color: parent.parent.checked ? Theme.textOnAccent : Theme.textSecondary
                        Behavior on x { NumberAnimation { duration: Theme.durationFast } }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Aspect Correction"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            Switch {
                id: aspectCorrectionSwitch
                checked: SettingsBridge.visualizerAspectCorrection
                onToggled: SettingsBridge.visualizerAspectCorrection = checked
                indicator: Rectangle {
                    implicitWidth: 40; implicitHeight: 20
                    x: parent.leftPadding; y: parent.topPadding + (parent.availableHeight - height) / 2
                    radius: 10; color: parent.checked ? Theme.accent : Theme.surfaceOverlay
                    border.color: Theme.border
                    Rectangle {
                        width: 16; height: 16; radius: 8
                        x: parent.checked ? parent.width - width - 2 : 2
                        y: (parent.height - height) / 2
                        color: parent.parent.checked ? Theme.textOnAccent : Theme.textSecondary
                        Behavior on x { NumberAnimation { duration: Theme.durationFast } }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // KARAOKE MASTER
        // ═══════════════════════════════════════════════════════════
        KaraokeSettings {
            id: karaokeSettings
            Layout.fillWidth: true
        }

        // ═══════════════════════════════════════════════════════════
        // RECORDING
        // ═══════════════════════════════════════════════════════════
        Text {
            text: "Recording"
            color: Theme.accent
            font: Theme.fontSubtitle
            Layout.topMargin: Theme.spacingSmall
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Video Resolution"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            ComboBox {
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
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
                contentItem: Text {
                    text: parent.displayText
                    color: Theme.textPrimary
                    font: Theme.fontCaption
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
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

        RowLayout {
            Layout.fillWidth: true
            Text { text: "FPS"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 10; to: 120; stepSize: 5
                value: SettingsBridge.recorderFps
                onValueModified: SettingsBridge.recorderFps = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "CRF (Quality)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 0; to: 51
                value: SettingsBridge.recorderCrf
                onValueModified: SettingsBridge.recorderCrf = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Encoder Preset"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            ComboBox {
                model: ["ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"]
                currentIndex: model.indexOf(SettingsBridge.recorderPreset)
                onActivated: SettingsBridge.recorderPreset = currentText
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
                contentItem: Text {
                    text: parent.displayText
                    color: Theme.textPrimary
                    font: Theme.fontCaption
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Audio Codec"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            ComboBox {
                id: audioCodecCombo
                model: ["aac", "opus", "mp3", "flac"]
                currentIndex: model.indexOf(SettingsBridge.recorderAudioCodec)
                onActivated: SettingsBridge.recorderAudioCodec = currentText
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
                contentItem: Text {
                    text: parent.displayText
                    color: Theme.textPrimary
                    font: Theme.fontCaption
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Audio Bitrate (kbps)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 64; to: 640; stepSize: 32
                value: SettingsBridge.recorderAudioBitrate
                onValueModified: SettingsBridge.recorderAudioBitrate = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // SUNO AI
        // ═══════════════════════════════════════════════════════════
        Text {
            text: "Suno AI"
            color: Theme.accent
            font: Theme.fontSubtitle
            Layout.topMargin: Theme.spacingSmall
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTiny
            Text { text: "Session Token"; color: Theme.textSecondary; font: Theme.fontCaption }
            TextField {
                Layout.fillWidth: true
                text: SettingsBridge.sunoToken
                onTextEdited: SettingsBridge.sunoToken = text
                placeholderText: "Enter Suno token..."
                echoMode: TextInput.PasswordEchoOnEdit
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: parent.activeFocus ? Theme.accent : Theme.border }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // KEYBOARD SHORTCUTS
        // ═══════════════════════════════════════════════════════════
        Text {
            text: "Keyboard Shortcuts"
            color: Theme.accent
            font: Theme.fontSubtitle
            Layout.topMargin: Theme.spacingSmall
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMedium
            rowSpacing: Theme.spacingSmall

            Text { text: "Play / Pause"; color: Theme.textSecondary; font: Theme.fontCaption }
            Text { text: SettingsBridge.keyboardPlayPause; color: Theme.textPrimary; font: Theme.fontBodyStrong }

            Text { text: "Next Track"; color: Theme.textSecondary; font: Theme.fontCaption }
            Text { text: SettingsBridge.keyboardNextTrack; color: Theme.textPrimary; font: Theme.fontBodyStrong }

            Text { text: "Previous Track"; color: Theme.textSecondary; font: Theme.fontCaption }
            Text { text: SettingsBridge.keyboardPrevTrack; color: Theme.textPrimary; font: Theme.fontBodyStrong }

            Text { text: "Toggle Record"; color: Theme.textSecondary; font: Theme.fontCaption }
            Text { text: SettingsBridge.keyboardToggleRecord; color: Theme.textPrimary; font: Theme.fontBodyStrong }

            Text { text: "Toggle Fullscreen"; color: Theme.textSecondary; font: Theme.fontCaption }
            Text { text: SettingsBridge.keyboardToggleFullscreen; color: Theme.textPrimary; font: Theme.fontBodyStrong }

            Text { text: "Next Preset"; color: Theme.textSecondary; font: Theme.fontCaption }
            Text { text: SettingsBridge.keyboardNextPreset; color: Theme.textPrimary; font: Theme.fontBodyStrong }

            Text { text: "Previous Preset"; color: Theme.textSecondary; font: Theme.fontCaption }
            Text { text: SettingsBridge.keyboardPrevPreset; color: Theme.textPrimary; font: Theme.fontBodyStrong }
        }

        Text {
            text: "Custom keybinding editor coming soon..."
            color: Theme.textDisabled
            font: Theme.fontCaption
            Layout.topMargin: Theme.spacingTiny
        }

        // ═══════════════════════════════════════════════════════════
        // PROFILE
        // ═══════════════════════════════════════════════════════════
        Text {
            text: "Profile"
            color: Theme.accent
            font: Theme.fontSubtitle
            Layout.topMargin: Theme.spacingSmall
        }

        Text {
            text: "Profiles coming soon..."
            color: Theme.textDisabled
            font: Theme.fontCaption
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            AppButton {
                text: "Export Profile"
                Layout.fillWidth: true
                enabled: false
                opacity: enabled ? 1.0 : 0.4
            }

            AppButton {
                text: "Import Profile"
                Layout.fillWidth: true
                enabled: false
                opacity: enabled ? 1.0 : 0.4
            }
        }

        // ═══════════════════════════════════════════════════════════
        // ACTIONS
        // ═══════════════════════════════════════════════════════════
        Item { Layout.fillHeight: true; Layout.preferredHeight: Theme.spacingLarge }

        AppButton {
            text: "Save & Apply"
            Layout.fillWidth: true
            onClicked: {
                Theme.applyAccent(accentInput.text)
                Theme.applyBackground(bgInput.text)
                SettingsBridge.save()
                console.log("Settings saved and applied.")
            }
        }

        AppButton {
            text: "Reset Defaults"
            Layout.fillWidth: true
            onClicked: SettingsBridge.resetToDefaults()
        }
    }
}
