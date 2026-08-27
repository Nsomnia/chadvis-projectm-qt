/**
 * @file SettingsView.qml
 * @brief Full-page settings surface hosting the shared SettingsPanel
 *
 * The panel itself (panels/SettingsPanel.qml) is untouched — it composes
 * the per-category settings components and owns all bridge bindings. This
 * view just gives it a proper page frame: title header + hairline rule.
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../components"
import "../panels"

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Page header ─────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            Layout.bottomMargin: Theme.spacingMedium
            spacing: 2

            Text {
                text: "Settings"
                color: Theme.textPrimary
                font: Theme.fontDisplay
            }

            Text {
                text: "Engine, recorder, appearance and Suno account — auto-saved as you tweak"
                color: Theme.textSecondary
                font: Theme.fontCaption
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        // ── Panel body ──────────────────────────────
        SettingsPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
