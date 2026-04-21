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
    contentHeight: contentLayout.implicitHeight
    clip: true

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
        // KARAOKE MASTER
        // ═══════════════════════════════════════════════════════════
        KaraokeSettings {
            id: karaokeSettings
            Layout.fillWidth: true
        }

        // ═══════════════════════════════════════════════════════════
        // ENGINE CONTROLS
        // ═══════════════════════════════════════════════════════════
        Text {
            text: "Engine Controls"
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
            Text { text: "Audio Buffer (ms)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 10; to: 1000; stepSize: 10
                value: SettingsBridge.audioBufferSize
                onValueModified: SettingsBridge.audioBufferSize = value
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
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
            Text { text: "CRF (Quality)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 0; to: 51; value: SettingsBridge.recorderCrf
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

        Item { Layout.fillHeight: true; Layout.preferredHeight: Theme.spacingLarge }

        AppButton {
            text: "Save & Apply"
            Layout.fillWidth: true
            onClicked: {
                Theme.accent = accentInput.text
                Theme.background = bgInput.text
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
