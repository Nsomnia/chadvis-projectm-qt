import QtQuick
import QtQuick.Layouts
import ChadVis

ColumnLayout {
    id: root

    property bool showGlow: true
    readonly property real verticalPosition: SettingsBridge.karaokeYPosition

    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    Text {
        text: "Karaoke Aesthetics"
        color: Theme.accent
        font: Theme.fontSubtitle
    }

    RowLayout {
        Layout.fillWidth: true

        Text {
            Layout.fillWidth: true
            text: "Glow effects"
            color: Theme.textPrimary
            font: Theme.fontBody
        }

        AppSwitch {
            checked: root.showGlow
            onToggled: root.showGlow = checked
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTiny

        Text {
            text: "Vertical alignment"
            color: Theme.textPrimary
            font: Theme.fontBody
        }

        AppSlider {
            Layout.fillWidth: true
            from: 0.1
            to: 0.9
            stepSize: 0.01
            value: root.verticalPosition
            onMoved: SettingsBridge.karaokeYPosition = value
        }
    }

    RowLayout {
        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTiny

            Text {
                text: "Accent color"
                color: Theme.textPrimary
                font: Theme.fontBody
            }

            Text {
                text: "Follows the current Appearance accent"
                color: Theme.textSecondary
                font: Theme.fontCaption
            }
        }

        Rectangle {
            Layout.preferredWidth: Theme.iconMedium
            Layout.preferredHeight: Theme.iconMedium
            radius: Theme.radiusSmall
            color: Theme.accent
            border.width: 1
            border.color: Theme.accentLight

            Accessible.role: Accessible.StaticText
            Accessible.name: "Current accent color"
        }
    }
}
