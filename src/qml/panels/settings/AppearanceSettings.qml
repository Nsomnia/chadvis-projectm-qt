/**
 * @file AppearanceSettings.qml
 * @brief Settings section: accent/background color inputs
 *
 * Exposes apply() so the parent panel's "Save & Apply" action can commit
 * the edited colors without reaching into internal control ids.
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    id: root
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Appearance"
    }

    function apply() {
        Theme.applyAccent(accentInput.text)
        Theme.applyBackground(bgInput.text)
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
            AppTextField {
                id: accentInput
                Layout.fillWidth: true
                text: Theme.accent
            }
        }

        Text { text: "Background"; color: Theme.textSecondary; font: Theme.fontCaption }
        RowLayout {
            Layout.fillWidth: true
            Rectangle { width: 20; height: 20; radius: 4; color: bgInput.text; border.color: Theme.border }
            AppTextField {
                id: bgInput
                Layout.fillWidth: true
                text: Theme.background
            }
        }
    }
}
