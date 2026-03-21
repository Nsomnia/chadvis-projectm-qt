import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../styles"
import "../components"

ColumnLayout {
    id: root

    property string searchQuery: ""
    property bool showSearch: false

    spacing: 0

    ToolBar {
        Layout.fillWidth: true
        background: Rectangle { color: Theme.surfaceVariant }

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Label {
                text: "Suno Library"
                color: Theme.onSurface
                font.pixelSize: Theme.fontSizeMedium
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                text: "\u{1F50D}"
                onClicked: root.showSearch = !root.showSearch
            }

            ToolButton {
                text: "\u{1F504}"
                enabled: !SunoBridge.isSyncing
                onClicked: SunoBridge.refreshLibrary()
            }

            ToolButton {
                text: "\u{1F4BE}"
                enabled: !SunoBridge.isSyncing && SunoBridge.isAuthenticated
                onClicked: SunoBridge.syncDatabase()
            }
        }
    }

    TextField {
        id: searchField
        Layout.fillWidth: true
        Layout.margins: Theme.spacingSmall
        visible: root.showSearch
        placeholderText: "Search songs..."
        text: root.searchQuery
        onTextChanged: SunoBridge.searchQuery = text

        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surface
            border.color: searchField.activeFocus ? Theme.primary : Theme.outline
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.margins: Theme.spacingMedium
        visible: !SunoBridge.isAuthenticated
        text: "Sign in to access your Suno library"
        color: Theme.onSurfaceVariant
        font.pixelSize: Theme.fontSizeMedium
        horizontalAlignment: Text.AlignHCenter

        AppButton {
            anchors.top: parent.bottom
            anchors.topMargin: Theme.spacingSmall
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Sign In"
            onClicked: SunoBridge.authenticate()
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.margins: Theme.spacingMedium
        visible: SunoBridge.isAuthenticated && SunoBridge.clips.length === 0 && !SunoBridge.isSyncing
        text: "No songs found. Sync your library."
        color: Theme.onSurfaceVariant
        font.pixelSize: Theme.fontSizeMedium
        horizontalAlignment: Text.AlignHCenter
    }

    Label {
        Layout.fillWidth: true
        Layout.margins: Theme.spacingMedium
        visible: SunoBridge.isSyncing
        text: SunoBridge.statusMessage || "Syncing..."
        color: Theme.primary
        font.pixelSize: Theme.fontSizeMedium
        horizontalAlignment: Text.AlignHCenter

        BusyIndicator {
            anchors.top: parent.bottom
            anchors.topMargin: Theme.spacingSmall
            anchors.horizontalCenter: parent.horizontalCenter
            running: SunoBridge.isSyncing
        }
    }

    ListView {
        id: clipList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        visible: SunoBridge.isAuthenticated && SunoBridge.clips.length > 0

        model: root.searchQuery.length > 0 ? SunoBridge.searchResults : SunoBridge.clips
        delegate: ClipDelegate {
            width: clipList.width
            onPlayClicked: SunoBridge.downloadAndPlay(modelData.id)
            onLyricsClicked: SunoBridge.fetchLyrics(modelData.id)
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        footer: Item {
            height: Theme.spacingLarge
            visible: !SunoBridge.isSyncing

            AppButton {
                anchors.centerIn: parent
                text: "Load More"
                visible: SunoBridge.totalClips > 0
                onClicked: SunoBridge.refreshLibrary()
            }
        }
    }

    component ClipDelegate: Rectangle {
        id: delegate
        height: 80
        color: mouseArea.containsMouse ? Theme.surfaceVariant : Theme.surface
        radius: Theme.radiusSmall

        property bool hasLyrics: modelData ? SunoBridge.hasLyrics(modelData.id) : false

        signal playClicked()
        signal lyricsClicked()

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            spacing: Theme.spacingSmall

            Image {
                source: modelData ? modelData.imageUrl : ""
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                fillMode: Image.PreserveAspectCrop

                Rectangle {
                    anchors.fill: parent
                    color: Theme.surface
                    visible: parent.status !== Image.Ready

                    Label {
                        anchors.centerIn: parent
                        text: "\u266B"
                        font.pixelSize: 24
                        color: Theme.onSurfaceVariant
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: modelData ? modelData.title : ""
                    color: Theme.onSurface
                    font.pixelSize: Theme.fontSizeMedium
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: modelData ? modelData.displayName : ""
                    color: Theme.onSurfaceVariant
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Row {
                    spacing: Theme.spacingSmall

                    Text {
                        text: modelData ? modelData.duration : ""
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.fontSizeSmall
                        visible: text !== ""
                    }

                    Text {
                        text: modelData ? modelData.bpm : ""
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.fontSizeSmall
                        visible: text !== ""
                    }

                    Text {
                        text: modelData && modelData.isInstrumental ? "Instrumental" : ""
                        color: Theme.accent
                        font.pixelSize: Theme.fontSizeSmall
                        visible: text !== ""
                    }
                }
            }

            AppButton {
                icon: "\u{1F3A5}"
                implicitWidth: 44
                implicitHeight: 44
                onClicked: delegate.lyricsClicked()
                visible: !delegate.hasLyrics
            }

            AppButton {
                icon: "\u25B6"
                implicitWidth: 44
                implicitHeight: 44
                highlighted: true
                onClicked: delegate.playClicked()
            }
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onDoubleClicked: delegate.playClicked()
        }
    }
}
