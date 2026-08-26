/**
 * @file ShortcutsSettings.qml
 * @brief Settings section: keyboard shortcut reference display
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Keyboard Shortcuts"
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
}
