pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../components"

Item {
    id: root

    property bool requested: false

    signal navigateRequested(string viewId)

    function ensureLoaded() {
        if (visible && !requested && !SunoBridge.notificationsLoading) {
            requested = true
            SunoBridge.refreshNotifications()
        }
    }

    onVisibleChanged: ensureLoaded()
    Component.onCompleted: ensureLoaded()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            Layout.bottomMargin: Theme.spacingMedium
            spacing: Theme.spacingMedium

            ColumnLayout {
                spacing: 2

                Text {
                    text: "Notifications"
                    color: Theme.textPrimary
                    font: Theme.fontDisplay
                }

                Text {
                    text: SunoBridge.notificationsError.length > 0
                          ? SunoBridge.notificationsError
                          : SunoBridge.unreadCount > 0
                            ? SunoBridge.unreadCount + " unread"
                            : SunoBridge.notifications.length + " notifications"
                    color: SunoBridge.notificationsError.length > 0 ? Theme.error : Theme.textSecondary
                    font: Theme.fontCaption
                }
            }

            Item { Layout.fillWidth: true }

            AppButton {
                flat: true
                implicitWidth: 40
                implicitHeight: 40
                enabled: !SunoBridge.notificationsLoading
                icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
                rotation: SunoBridge.notificationsLoading ? 360 : 0

                Behavior on rotation {
                    SequentialAnimation {
                        NumberAnimation {
                            duration: 900
                            easing.type: Easing.InOutCubic
                        }
                    }
                }

                ToolTip.visible: hovered
                ToolTip.text: "Refresh notifications"
                ToolTip.delay: 400
                onClicked: SunoBridge.refreshNotifications()
            }

            AppButton {
                text: "Mark all read"
                flat: true
                enabled: SunoBridge.unreadCount > 0 && !SunoBridge.notificationsLoading
                onClicked: SunoBridge.markAllNotificationsRead()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        Item {
            id: notificationContent
            Layout.fillWidth: true
            Layout.fillHeight: true

        ListView {
            id: notificationList
            anchors.fill: parent
            clip: true
            model: SunoBridge.notifications
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.spacingSmall

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: notificationCard

                required property var modelData
                readonly property bool unread: !modelData.is_read

                width: notificationList.width
                height: notificationBody.implicitHeight + Theme.spacingMedium * 2
                radius: Theme.radiusMedium
                color: notificationCard.unread ? Theme.surfaceRaised : Theme.backgroundAlt
                border.width: 1
                border.color: notificationCard.unread ? Theme.accent : Theme.border

                ColumnLayout {
                    id: notificationBody
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingTiny

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            Layout.fillWidth: true
                            text: notificationCard.modelData.author_name || "Suno"
                            color: Theme.textPrimary
                            font: Theme.fontBodyStrong
                            elide: Text.ElideRight
                        }

                        Text {
                            text: notificationCard.modelData.updated_at || ""
                            color: Theme.textDisabled
                            font: Theme.fontTiny
                            elide: Text.ElideRight
                            Layout.maximumWidth: 180
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: notificationCard.modelData.author_handle !== ""
                        text: "@" + notificationCard.modelData.author_handle
                        color: Theme.textSecondary
                        font: Theme.fontCaption
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: notificationCard.modelData.type !== ""
                        text: notificationCard.modelData.type
                        color: Theme.textDisabled
                        font: Theme.fontTiny
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: notificationCard.modelData.caption !== ""
                        text: notificationCard.modelData.caption
                        color: Theme.textPrimary
                        font: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: notificationCard.modelData.content_title !== ""
                        text: notificationCard.modelData.content_title
                        color: notificationCard.unread ? Theme.accent : Theme.textSecondary
                        font: Theme.fontBodyStrong
                        elide: Text.ElideRight
                    }
                }
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - Theme.spacingLarge * 2
            visible: notificationList.count === 0 && !SunoBridge.notificationsLoading
            spacing: Theme.spacingMedium

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: {
                    if (SunoBridge.notificationsError.length > 0)
                        return "Notifications could not be loaded.\n" + SunoBridge.notificationsError
                    if (!SunoBridge.isAuthenticated)
                        return "Not signed in.\nSign in via Settings → Suno AI to load notifications."
                    return "No notifications yet."
                }
                color: SunoBridge.notificationsError.length > 0 ? Theme.error : Theme.textDisabled
                font: Theme.fontBody
            }

            AppButton {
                Layout.alignment: Qt.AlignHCenter
                visible: SunoBridge.notificationsError.length > 0
                text: "Retry"
                flat: true
                onClicked: SunoBridge.refreshNotifications()
            }

            AppButton {
                Layout.alignment: Qt.AlignHCenter
                visible: SunoBridge.notificationsError.length === 0 && !SunoBridge.isAuthenticated
                text: "Open Settings → Suno AI"
                onClicked: root.navigateRequested("settings")
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            visible: notificationList.count === 0 && SunoBridge.notificationsLoading
            spacing: Theme.spacingSmall

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: "Loading notifications…"
                color: Theme.textSecondary
                font: Theme.fontCaption
                Layout.alignment: Qt.AlignHCenter
            }
        }
        }
    }
}
