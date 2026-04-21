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
        // APPEARANCE
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

        CheckBox {
            text: "High Quality FFT (PFFFT)"
            checked: true
            font: Theme.fontBody
            contentItem: Text { text: parent.text; font: parent.font; color: Theme.textPrimary; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
        }

        CheckBox {
            id: gaplessToggle
            text: "Gapless Playback (Preload Next)"
            checked: true
            font: Theme.fontBody
            contentItem: Text { text: parent.text; font: parent.font; color: Theme.textPrimary; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Mesh Complexity"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            ComboBox {
                model: ["Low (32x32)", "Medium (64x64)", "High (128x128)", "Ultra (256x256)"]
                currentIndex: 1
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Audio Buffer (ms)"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            SpinBox {
                from: 10; to: 500; value: 100; stepSize: 10
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
            Text { text: "Encoder Profile"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            ComboBox {
                model: ["Fast (Low Latency)", "Balanced", "Slow (Max Quality)"]
                currentIndex: 1
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Output Format"; color: Theme.textSecondary; font: Theme.fontCaption; Layout.fillWidth: true }
            ComboBox {
                model: ["MP4 (H.264)", "WebM (VP9)", "MOV (ProRes)"]
                currentIndex: 0
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
            }
        }

        Item { Layout.fillHeight: true; Layout.preferredHeight: Theme.spacingLarge }

        AppButton {
            text: "Save & Apply"
            Layout.fillWidth: true
            onClicked: {
                Theme.accent = accentInput.text
                Theme.background = bgInput.text
                console.log("Settings saved and applied.")
            }
        }
        
        AppButton {
            text: "Reset Defaults"
            Layout.fillWidth: true
            onClicked: console.log("Resetting to factory defaults...")
        }
    }
}
