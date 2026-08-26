/**
 * @file SunoSettings.qml
 * @brief Settings section: Suno AI session token
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Suno AI"
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTiny
        Text { text: "Session Token"; color: Theme.textSecondary; font: Theme.fontCaption }
        AppTextField {
            Layout.fillWidth: true
            text: SettingsBridge.sunoToken
            onTextEdited: SettingsBridge.sunoToken = text
            placeholderText: "Enter Suno token..."
            echoMode: TextInput.PasswordEchoOnEdit
        }
    }
}
