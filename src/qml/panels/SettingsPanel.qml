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

    spacing: Theme.spacingLarge

    // ═══════════════════════════════════════════════════════════
    // UI SETTINGS
    // ═══════════════════════════════════════════════════════════

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMedium

        Text {
            text: "Appearance"
            color: Theme.textPrimary
            font: Theme.fontTitle
        }

        // Accent Color Setting
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text {
                text: "Accent Color"
                color: Theme.textSecondary
                font: Theme.fontBody
                Layout.preferredWidth: 120
            }

            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: Theme.radiusSmall
                color: accentColorInput.text
                border.color: Theme.border
                border.width: 1

                // Fallback to Theme accent if input is invalid
                onColorChanged: {
                    if (color.toString() === "#000000" && accentColorInput.text !== "#000000") {
                        color = Theme.accent
                    }
                }
            }

            TextField {
                id: accentColorInput
                Layout.fillWidth: true
                text: Theme.accent.toString()
                placeholderText: "#RRGGBB"
                
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                
                background: Rectangle {
                    color: Theme.surfaceRaised
                    radius: Theme.radiusSmall
                    border.color: accentColorInput.activeFocus ? Theme.borderFocus : Theme.border
                    border.width: 1
                }
                
                leftPadding: Theme.spacingSmall
                rightPadding: Theme.spacingSmall
            }
        }

        // Background Color Setting
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text {
                text: "Background Color"
                color: Theme.textSecondary
                font: Theme.fontBody
                Layout.preferredWidth: 120
            }

            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: Theme.radiusSmall
                color: bgColorInput.text
                border.color: Theme.border
                border.width: 1
                
                onColorChanged: {
                    if (color.toString() === "#000000" && bgColorInput.text !== "#000000") {
                        color = Theme.background
                    }
                }
            }

            TextField {
                id: bgColorInput
                Layout.fillWidth: true
                text: Theme.background.toString()
                placeholderText: "#RRGGBB"
                
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                
                background: Rectangle {
                    color: Theme.surfaceRaised
                    radius: Theme.radiusSmall
                    border.color: bgColorInput.activeFocus ? Theme.borderFocus : Theme.border
                    border.width: 1
                }
                
                leftPadding: Theme.spacingSmall
                rightPadding: Theme.spacingSmall
            }
        }
    }

    Item {
        // Spacer to push everything to the top
        Layout.fillHeight: true
    }

    // Apply Button
    RowLayout {
        Layout.fillWidth: true
        
        Item { Layout.fillWidth: true } // Spacer
        
        AppButton {
            text: "Apply Changes"
            implicitWidth: 140
            implicitHeight: Theme.buttonHeight
            radius: Theme.radiusMedium
            // highlighted: true
            
            onClicked: {
                console.log("Apply color settings: Accent=" + accentColorInput.text + " Bg=" + bgColorInput.text)
                Theme.accent = accentColorInput.text
                Theme.background = bgColorInput.text
            }
        }
    }
}
