/**
 * @file SettingsPanel.qml
 * @brief Slim settings composer
 *
 * Each category lives in its own component under panels/settings/.
 * Section order and all SettingsBridge bindings are preserved from the
 * original monolithic panel:
 *   Performance Presets → Appearance → Audio Engine → Visualizer Engine
 *   → Karaoke Master → Recording → Suno AI → Keyboard Shortcuts → Profile
 *   → Actions
 *
 * @version 2.0.0 - Split into per-category components
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"
import "settings"

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

        PerformanceSettings {}

        AppearanceSettings {
            id: appearanceSettings
        }

        AudioSettings {}

        VisualizerSettings {}

        // ═══════════════════════════════════════════════════════════
        // KARAOKE MASTER
        // ═══════════════════════════════════════════════════════════
        KaraokeSettings {
            id: karaokeSettings
            Layout.fillWidth: true
        }

        RecordingSettings {}

        SunoSettings {}

        ShortcutsSettings {}

        ProfileSettings {}

        // ═══════════════════════════════════════════════════════════
        // ACTIONS
        // ═══════════════════════════════════════════════════════════
        Item { Layout.fillHeight: true; Layout.preferredHeight: Theme.spacingLarge }

        AppButton {
            text: "Save & Apply"
            Layout.fillWidth: true
            onClicked: {
                appearanceSettings.apply()
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
