import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis

/// The account's playlists, as a one-row strip above the library grid.
///
/// Reads only: `openPlaylist` swaps the grid to that playlist's clips and
/// `closePlaylist` swaps it back. A playlist is a lens on the same grid, not a
/// second page, so nothing here duplicates ClipCard or the save/select state
/// the view above owns.
Item {
    id: root

    readonly property bool browsing: SunoBridge.openPlaylistId !== ""
    implicitHeight: browsing ? 34 : 78

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        // ── Open playlist: one row, one way back ─────────────────────────
        RowLayout {
            Layout.fillWidth: true
            visible: root.browsing
            spacing: Theme.spacingSmall

            AppButton {
                text: "‹ Playlists"
                flat: true
                onClicked: SunoBridge.closePlaylist()
                Accessible.name: "Back to all playlists"
            }

            Text {
                Layout.fillWidth: true
                text: SunoBridge.openPlaylistName !== ""
                      ? SunoBridge.openPlaylistName
                      : SunoBridge.openPlaylistId
                color: Theme.textPrimary
                font: Theme.fontCaptionStrong
                elide: Text.ElideRight
            }

            Text {
                text: SunoBridge.playlistClips.length + " tracks"
                color: Theme.textSecondary
                font: Theme.fontCaption
            }

            AppButton {
                text: "Load more"
                flat: true
                visible: SunoBridge.playlistHasMore
                onClicked: SunoBridge.requestNextPlaylistPage()
            }
        }

        // ── The list itself ──────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.browsing
            spacing: Theme.spacingSmall

            Text {
                text: "Playlists"
                color: Theme.textSecondary
                font: Theme.fontCaptionStrong
            }

            AppButton {
                icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
                flat: true
                implicitWidth: 30
                implicitHeight: 30
                rotation: SunoBridge.playlistsLoading ? 360 : 0
                Behavior on rotation {
                    SequentialAnimation {
                        NumberAnimation { duration: 900; easing.type: Easing.InOutCubic }
                    }
                }
                ToolTip.visible: hovered
                ToolTip.text: "Refresh playlists"
                ToolTip.delay: 400
                onClicked: SunoBridge.refreshPlaylists()
                Accessible.name: "Refresh playlists"
            }

            ListView {
                id: strip
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: ListView.Horizontal
                spacing: Theme.spacingSmall
                clip: true
                model: SunoBridge.playlists

                delegate: Rectangle {
                    required property var modelData
                    width: Math.min(220, chipText.implicitWidth + Theme.spacingLarge * 2)
                    height: strip.height - 6
                    radius: Theme.radiusSmall
                    color: chipMouse.containsMouse
                           ? Theme.surfaceRaised : Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        width: parent.width - Theme.spacingLarge
                        text: (modelData.name && modelData.name !== "")
                              ? modelData.name + "  ·  " + (modelData.song_count || 0)
                              : "Untitled playlist"
                        color: Theme.textPrimary
                        font: Theme.fontCaption
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        id: chipMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: SunoBridge.openPlaylist(modelData.id)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: SunoBridge.playlists.length === 0 && !SunoBridge.playlistsLoading
                text: SunoBridge.playlistsError !== ""
                      ? SunoBridge.playlistsError
                      : "No playlists yet"
                color: SunoBridge.playlistsError !== ""
                       ? Theme.warning : Theme.textSecondary
                font: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }
}
