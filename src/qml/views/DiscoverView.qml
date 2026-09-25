pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "../components"

Item {
    id: root

    signal navigateRequested(string viewId)

    function ensureLoaded() {
        if (visible && !SunoBridge.discoverLoaded && !SunoBridge.discoverLoading)
            SunoBridge.refreshDiscover()
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
                    text: "Explore"
                    color: Theme.textPrimary
                    font: Theme.fontDisplay
                }

                Text {
                    text: SunoBridge.discoverLoaded
                          ? SunoBridge.discover.length + " feed" + (SunoBridge.discover.length === 1 ? "" : "s")
                          : "Browse capture-backed Explore feeds"
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                }
            }

            Item { Layout.fillWidth: true }

            AppButton {
                flat: true
                implicitWidth: 40
                implicitHeight: 40
                enabled: !SunoBridge.discoverLoading
                icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
                rotation: SunoBridge.discoverLoading ? 360 : 0

                Behavior on rotation {
                    SequentialAnimation {
                        NumberAnimation {
                            duration: 900
                            easing.type: Easing.InOutCubic
                        }
                    }
                }

                ToolTip.visible: hovered
                ToolTip.text: "Refresh Explore"
                ToolTip.delay: 400
                onClicked: SunoBridge.refreshDiscover()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ListView {
            id: feedList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: SunoBridge.discover
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: feedDelegate

                required property var modelData
                readonly property var feedClips: modelData.clips || []

                width: feedList.width
                height: feedBody.implicitHeight + Theme.spacingLarge

                ColumnLayout {
                    id: feedBody
                    x: Theme.spacingLarge
                    width: parent.width - x * 2
                    spacing: Theme.spacingMedium

                    Text {
                        Layout.fillWidth: true
                        text: feedDelegate.modelData.label
                        color: Theme.textPrimary
                        font: Theme.fontTitle
                        elide: Text.ElideRight
                    }

                    GridView {
                        id: clipGrid
                        Layout.fillWidth: true
                        Layout.preferredHeight: rows * cellHeight
                        visible: count > 0
                        interactive: false
                        boundsBehavior: Flickable.StopAtBounds
                        model: feedDelegate.feedClips

                        readonly property int columns: Math.max(1, Math.floor(width / Theme.cardTileMinimum))
                        readonly property int rows: Math.ceil(count / columns)
                        cellWidth: Math.floor(width / columns)
                        cellHeight: cellWidth * 1.12 + Theme.spacingSmall

                        delegate: Rectangle {
                            id: clipCard
                            required property var modelData

                            width: clipGrid.cellWidth - Theme.spacingSmall
                            height: clipGrid.cellHeight - Theme.spacingSmall
                            radius: Theme.radiusMedium
                            color: Theme.surfaceRaised
                            border.color: Theme.border
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.spacingMedium
                                spacing: Theme.spacingSmall

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.max(112, (clipCard.width - Theme.spacingMedium * 2) * 0.6)
                                    radius: Theme.radiusSmall
                                    color: Theme.surfaceOverlay
                                    clip: true

                                    Image {
                                        id: clipImage
                                        anchors.fill: parent
                                        source: clipCard.modelData.image_url || ""
                                        sourceSize: Qt.size(360, 240)
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectCrop
                                        visible: status === Image.Ready
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Suno"
                                        color: Theme.textDisabled
                                        font: Theme.fontSubtitle
                                        visible: clipImage.status !== Image.Ready
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: clipCard.modelData.title || "Untitled clip"
                                    color: Theme.textPrimary
                                    font: Theme.fontBodyStrong
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: clipCard.modelData.creator || "Unknown creator"
                                    color: Theme.textSecondary
                                    font: Theme.fontCaption
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: feedDelegate.feedClips.length === 0
                        text: "No clip cards in this feed."
                        color: Theme.textDisabled
                        font: Theme.fontBody
                    }
                }
            }

            onAtYEndChanged: {
                if (visible && atYEnd && SunoBridge.discoverHasMore && !SunoBridge.discoverLoading)
                    SunoBridge.loadMoreDiscover()
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - Theme.spacingLarge * 2
                visible: feedList.count === 0 && !SunoBridge.discoverLoading
                spacing: Theme.spacingMedium

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: {
                        if (SunoBridge.discoverError.length > 0)
                            return "Explore could not be loaded.\n" + SunoBridge.discoverError
                        if (!SunoBridge.isAuthenticated)
                            return "Not signed in.\nSign in via Settings → Suno AI to load Explore."
                        if (SunoBridge.discoverLoaded)
                            return "Explore returned no feeds."
                        return "Refresh to load Explore."
                    }
                    color: SunoBridge.discoverError.length > 0 ? Theme.error : Theme.textDisabled
                    font: Theme.fontBody
                }

                AppButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: SunoBridge.discoverError.length > 0
                    text: "Retry"
                    flat: true
                    onClicked: SunoBridge.refreshDiscover()
                }

                AppButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: SunoBridge.discoverError.length === 0 && !SunoBridge.isAuthenticated
                    text: "Open Settings → Suno AI"
                    onClicked: root.navigateRequested("settings")
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: feedList.count === 0 && SunoBridge.discoverLoading
                spacing: Theme.spacingSmall

                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: "Loading Explore…"
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: Theme.backgroundAlt

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLarge
                anchors.rightMargin: Theme.spacingLarge
                spacing: Theme.spacingMedium

                Text {
                    Layout.fillWidth: true
                    text: {
                        if (SunoBridge.discoverLoading) return "Loading Explore…"
                        if (SunoBridge.discoverError.length > 0) return SunoBridge.discoverError
                        if (SunoBridge.discoverHasMore) return "More Explore feeds available"
                        if (SunoBridge.discoverLoaded) return "End of Explore"
                        return "Explore has not loaded"
                    }
                    color: SunoBridge.discoverError.length > 0 ? Theme.error : Theme.textDisabled
                    font: Theme.fontCaption
                    elide: Text.ElideRight
                }

                AppButton {
                    visible: SunoBridge.discoverHasMore
                    enabled: !SunoBridge.discoverLoading
                    text: SunoBridge.discoverLoading ? "Loading…" : "Load more"
                    flat: true
                    onClicked: SunoBridge.loadMoreDiscover()
                }
            }
        }
    }
}
