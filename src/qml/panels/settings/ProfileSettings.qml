/**
 * @file ProfileSettings.qml
 * @brief Settings section: profile import/export (placeholder UI)
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Profile"
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
}
