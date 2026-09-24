/**
 * @file SunoSettings.qml
 * @brief Settings section: Suno session cookie header credential
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    id: root

    readonly property bool needsCookieHeaderHint: {
        const value = SettingsBridge.sunoToken
        return typeof value === "string" && value.length > 0 && value.indexOf("=") < 0
    }

    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Session Cookie Header"
    }

    Text {
        Layout.fillWidth: true
        text: "Paste the complete Cookie request header copied from a signed-in browser's request to auth.suno.com/v1/client. The __client… cookies are the part that matters; a lone session JWT is not enough. Extra cookies are ignored."
        color: Theme.textSecondary
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        text: "Stored in the OS keychain; never written to the config file or logs."
        color: Theme.textSecondary
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        text: "Complete Cookie request header"
        color: Theme.textSecondary
        font: Theme.fontCaptionStrong
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 108

        TextArea {
            id: cookieHeaderInput
            anchors.fill: parent
            anchors.margins: 1
            text: SettingsBridge.sunoToken
            onTextEdited: SettingsBridge.sunoToken = text
            placeholderText: "Cookie: __client…=…; __client_uat…=…"
            color: Theme.textPrimary
            font: Theme.fontBody
            wrapMode: Text.WrapAnywhere
            selectByMouse: true
            padding: Theme.spacingSmall

            background: Rectangle {
                color: Theme.surfaceRaised
                radius: Theme.radiusSmall
                border.color: cookieHeaderInput.activeFocus ? Theme.accent : Theme.border
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Theme.radiusSmall
            color: Theme.surfaceRaised
            border.width: 1
            border.color: cookieHeaderInput.activeFocus ? Theme.accent : Theme.border
            visible: cookieHeaderInput.text.length > 0
            enabled: false
            z: 1

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingMedium
                anchors.verticalCenter: parent.verticalCenter
                text: "Hidden credential — paste remains intact"
                color: Theme.textSecondary
                font: Theme.fontBody
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.needsCookieHeaderHint
        text: "This field wants a cookie header, not a bare token."
        color: Theme.warning
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }
}
