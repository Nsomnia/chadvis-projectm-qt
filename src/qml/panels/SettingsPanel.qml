/**
 * @file SettingsPanel.qml
 * @brief Settings configuration panel
 *
 * Allows users to modify UI accent and background colors at runtime.
 *
 * @version 1.0.0 - 2026-04-19
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

ColumnLayout {
    id: root

    spacing: Theme.spacingMedium

    // ═══════════════════════════════════════════════════════════
    // UI CUSTOMIZATION
    // ═══════════════════════════════════════════════════════════

    Text {
        text: "Appearance"
        color: Theme.accent
        font: Theme.fontSubtitle
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.spacingMedium
        rowSpacing: Theme.spacingSmall

        Text {
            text: "Accent"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            
            Rectangle {
                width: 24; height: 24
                radius: Theme.radiusSmall
                color: accentInput.text
                border.color: Theme.border
            }

            TextField {
                id: accentInput
                Layout.fillWidth: true
                text: Theme.accent
                font: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        Text {
            text: "Background"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            
            Rectangle {
                width: 24; height: 24
                radius: Theme.radiusSmall
                color: bgInput.text
                border.color: Theme.border
            }

            TextField {
                id: bgInput
                Layout.fillWidth: true
                text: Theme.background
                font: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // VISUALIZER SETTINGS
    // ═══════════════════════════════════════════════════════════

    Text {
        text: "Engine"
        color: Theme.accent
        font: Theme.fontSubtitle
        Layout.topMargin: Theme.spacingMedium
    }

    CheckBox {
        text: "High Quality FFT"
        checked: true
        font: Theme.fontBody
        contentItem: Text {
            text: parent.text
            font: parent.font
            color: Theme.textPrimary
            leftPadding: parent.indicator.width + parent.spacing
            verticalAlignment: Text.AlignVCenter
        }
    }

    CheckBox {
        text: "Show FPS Counter"
        checked: false
        font: Theme.fontBody
        contentItem: Text {
            text: parent.text
            font: parent.font
            color: Theme.textPrimary
            leftPadding: parent.indicator.width + parent.spacing
            verticalAlignment: Text.AlignVCenter
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Mesh Size"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
        ComboBox {
            model: ["32x32", "64x64", "128x128"]
            currentIndex: 1
            background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
        }
    }

    Item { Layout.fillHeight: true }

    AppButton {
        text: "Save Configuration"
        Layout.fillWidth: true
        implicitHeight: 40
        onClicked: {
            Theme.accent = accentInput.text
            Theme.background = bgInput.text
        }
    }
}
