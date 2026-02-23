import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    id: root
    anchors.fill: parent

    property string expandedSection: "suno"
    property color backgroundTop: "#0F1A2A"
    property color backgroundBottom: "#152236"
    property color cardColor: "#1A2A3D"
    property color accent: "#30C6B5"

    function formatMs(value) {
        if (!value || value <= 0)
            return "00:00"

        var total = Math.floor(value / 1000)
        var minutes = Math.floor(total / 60)
        var seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" + seconds : seconds)
    }

    gradient: Gradient {
        GradientStop { position: 0.0; color: root.backgroundTop }
        GradientStop { position: 1.0; color: root.backgroundBottom }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: workspaceColumn.implicitHeight + 32
        clip: true

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        Column {
            id: workspaceColumn
            width: parent.width
            spacing: 12
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16

            Rectangle {
                width: parent.width
                radius: 20
                color: root.cardColor
                border.width: 1
                border.color: "#2E4560"
                implicitHeight: 146

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: "Suno Remote Playback Workspace"
                        color: "#F4FAFF"
                        font.pixelSize: 24
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Large mobile-style controls for Suno library sync, AI track playback, and video session flow"
                        wrapMode: Text.WordWrap
                        color: "#A9C2DA"
                        font.pixelSize: 13
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Rectangle {
                            Layout.preferredHeight: 30
                            Layout.fillWidth: true
                            radius: 11
                            color: "#132335"

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                text: sunoVm.statusMessage
                                color: "#D5ECFF"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 102
                            Layout.preferredHeight: 30
                            radius: 11
                            color: sunoVm.syncing ? "#2A6A53" : "#2B445E"

                            Text {
                                anchors.centerIn: parent
                                text: sunoVm.syncing ? "SYNCING" : "READY"
                                color: "#EAF8FF"
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                    }
                }
            }

            AccordionSection {
                sectionId: "suno"
                title: "Suno Library"
                subtitle: "Remote clips, search, and one-tap queue"
                expanded: root.expandedSection === sectionId
                backgroundColor: root.cardColor
                accentColor: root.accent
                width: parent.width
                onToggleRequested: root.expandedSection = (root.expandedSection === sectionId ? "" : sectionId)

                RowLayout {
                    spacing: 8

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: "Search Suno titles, tags, or artists"
                        text: sunoVm.searchQuery
                        onTextEdited: sunoVm.searchQuery = text
                        font.pixelSize: 15
                        selectByMouse: true
                    }

                    Button {
                        text: "Sync"
                        implicitHeight: 44
                        onClicked: sunoVm.refresh()
                    }

                    Button {
                        text: "Auth"
                        implicitHeight: 44
                        onClicked: sunoVm.authenticate()
                    }
                }

                Text {
                    text: "Showing " + sunoVm.clipCount + " of " + sunoVm.totalClipCount + " clips | Lyrics ready: " + sunoVm.lyricsReadyCount
                    color: "#A9C2DA"
                    font.pixelSize: 12
                }

                ListView {
                    id: sunoList
                    model: sunoVm.model
                    clip: true
                    spacing: 8
                    implicitHeight: Math.max(120, Math.min(420, contentHeight))
                    visible: count > 0

                    delegate: Rectangle {
                        width: sunoList.width
                        height: 96
                        radius: 14
                        color: "#101D2C"
                        border.width: 1
                        border.color: "#2F4A64"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            Rectangle {
                                Layout.preferredWidth: 74
                                Layout.preferredHeight: 74
                                radius: 10
                                color: "#24384F"

                                Text {
                                    anchors.centerIn: parent
                                    text: hasLyrics ? "LYR" : "SUNO"
                                    color: "#D8EEFF"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: title
                                    color: "#F4FAFF"
                                    font.pixelSize: 16
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: artist + "  |  " + duration + "  |  " + status
                                    color: "#9BB8D3"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: tags.length > 0 ? tags : lyricsPreview
                                    color: "#8AAAC8"
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
                                text: "Play"
                                implicitWidth: 78
                                implicitHeight: 44
                                onClicked: sunoVm.playClipById(clipId)
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    implicitHeight: 76
                    radius: 12
                    color: "#101D2C"
                    border.width: 1
                    border.color: "#2F4A64"
                    visible: sunoList.count === 0

                    Text {
                        anchors.centerIn: parent
                        text: sunoVm.syncing ? "Syncing remote Suno library..." : "No Suno clips in view. Sync or clear search."
                        color: "#A6C1DA"
                        font.pixelSize: 13
                    }
                }
            }

            AccordionSection {
                sectionId: "playback"
                title: "Playback"
                subtitle: "Transport, volume, and mode controls"
                expanded: root.expandedSection === sectionId
                backgroundColor: root.cardColor
                accentColor: "#48A6FF"
                width: parent.width
                onToggleRequested: root.expandedSection = (root.expandedSection === sectionId ? "" : sectionId)

                Rectangle {
                    width: parent.width
                    implicitHeight: 86
                    radius: 12
                    color: "#101D2C"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10

                        Text {
                            text: playbackVm.trackTitle
                            color: "#F4FAFF"
                            font.pixelSize: 16
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: playbackVm.trackArtist
                            color: "#9BB8D3"
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }

                Slider {
                    from: 0
                    to: Math.max(1, playbackVm.durationMs)
                    value: playbackVm.positionMs
                    onMoved: playbackVm.seek(value)
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: root.formatMs(playbackVm.positionMs)
                        color: "#9BB8D3"
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.formatMs(playbackVm.durationMs)
                        color: "#9BB8D3"
                        font.pixelSize: 12
                    }
                }

                RowLayout {
                    spacing: 10

                    Button {
                        text: "Prev"
                        implicitHeight: 46
                        implicitWidth: 90
                        onClicked: playbackVm.previous()
                    }

                    Button {
                        text: playbackVm.playing ? "Pause" : "Play"
                        implicitHeight: 46
                        implicitWidth: 120
                        onClicked: playbackVm.playPause()
                    }

                    Button {
                        text: "Next"
                        implicitHeight: 46
                        implicitWidth: 90
                        onClicked: playbackVm.next()
                    }

                    Button {
                        text: "Stop"
                        implicitHeight: 46
                        implicitWidth: 90
                        onClicked: playbackVm.stop()
                    }
                }

                RowLayout {
                    spacing: 8

                    Text {
                        text: "Volume"
                        color: "#A8C2DB"
                        font.pixelSize: 12
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: playbackVm.volume
                        onMoved: playbackVm.volume = value
                    }

                    Text {
                        text: Math.round(playbackVm.volume * 100) + "%"
                        color: "#CFE4F8"
                        font.pixelSize: 12
                    }
                }

                RowLayout {
                    spacing: 10

                    Switch {
                        text: "Shuffle"
                        checked: playbackVm.shuffleEnabled
                        onClicked: playbackVm.shuffleEnabled = checked
                    }

                    Button {
                        text: playbackVm.repeatModeLabel
                        implicitHeight: 40
                        onClicked: playbackVm.cycleRepeatMode()
                    }
                }
            }

            AccordionSection {
                sectionId: "playlist"
                title: "Playlist"
                subtitle: "Local + downloaded queue"
                expanded: root.expandedSection === sectionId
                backgroundColor: root.cardColor
                accentColor: "#C788FF"
                width: parent.width
                onToggleRequested: root.expandedSection = (root.expandedSection === sectionId ? "" : sectionId)

                ListView {
                    id: playlistView
                    model: playlistModel
                    clip: true
                    spacing: 6
                    implicitHeight: Math.max(120, Math.min(340, contentHeight))
                    visible: count > 0

                    delegate: Rectangle {
                        width: playlistView.width
                        height: 58
                        radius: 12
                        color: isCurrent ? "#2C415D" : "#101D2C"
                        border.width: 1
                        border.color: isCurrent ? "#6BB4FF" : "#2F4A64"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                Text {
                                    text: title
                                    color: "#F4FAFF"
                                    font.pixelSize: 14
                                    font.bold: isCurrent
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: artist
                                    color: "#9AB7D2"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Text {
                                text: durationText
                                color: "#BFD7EE"
                                font.pixelSize: 12
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: playlistModel.jumpTo(index)
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    implicitHeight: 74
                    radius: 12
                    color: "#101D2C"
                    border.width: 1
                    border.color: "#2F4A64"
                    visible: playlistView.count === 0

                    Text {
                        anchors.centerIn: parent
                        text: "Playlist is empty. Drop files or play from Suno."
                        color: "#A6C1DA"
                        font.pixelSize: 13
                    }
                }
            }

            AccordionSection {
                sectionId: "visualizer"
                title: "Visualizer"
                subtitle: "Preset navigation and lock"
                expanded: root.expandedSection === sectionId
                backgroundColor: root.cardColor
                accentColor: "#FDB44A"
                width: parent.width
                onToggleRequested: root.expandedSection = (root.expandedSection === sectionId ? "" : sectionId)

                Text {
                    text: visualizerVm.presetName
                    color: "#F4FAFF"
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideRight
                }

                RowLayout {
                    spacing: 10

                    Button {
                        text: "Prev"
                        implicitHeight: 44
                        implicitWidth: 92
                        onClicked: visualizerVm.previousPreset()
                    }

                    Button {
                        text: "Random"
                        implicitHeight: 44
                        implicitWidth: 110
                        onClicked: visualizerVm.randomPreset()
                    }

                    Button {
                        text: "Next"
                        implicitHeight: 44
                        implicitWidth: 92
                        onClicked: visualizerVm.nextPreset()
                    }

                    Switch {
                        text: "Lock"
                        checked: visualizerVm.presetLocked
                        onClicked: visualizerVm.presetLocked = checked
                    }
                }
            }

            AccordionSection {
                sectionId: "recording"
                title: "Recording"
                subtitle: "Single-tap capture for AI music videos"
                expanded: root.expandedSection === sectionId
                backgroundColor: root.cardColor
                accentColor: "#FF6363"
                width: parent.width
                onToggleRequested: root.expandedSection = (root.expandedSection === sectionId ? "" : sectionId)

                Rectangle {
                    width: parent.width
                    implicitHeight: 82
                    radius: 12
                    color: recordingVm.recording ? "#3B1C24" : "#102032"
                    border.width: 1
                    border.color: recordingVm.recording ? "#FF6666" : "#30506D"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12

                        ColumnLayout {
                            Layout.fillWidth: true

                            Text {
                                text: recordingVm.recording ? "Recording in progress" : "Recorder ready"
                                color: "#F4FAFF"
                                font.pixelSize: 17
                                font.bold: true
                            }

                            Text {
                                text: recordingVm.statusMessage
                                color: "#A9C2DA"
                                font.pixelSize: 12
                            }
                        }

                        Button {
                            text: recordingVm.recording ? "Stop" : "Start"
                            implicitWidth: 98
                            implicitHeight: 48
                            onClicked: recordingVm.toggle()
                        }
                    }
                }
            }

            AccordionSection {
                sectionId: "lyrics"
                title: "Karaoke and Lyrics"
                subtitle: "Suno-aligned lyric preview workflow"
                expanded: root.expandedSection === sectionId
                backgroundColor: root.cardColor
                accentColor: "#67D779"
                width: parent.width
                onToggleRequested: root.expandedSection = (root.expandedSection === sectionId ? "" : sectionId)

                Text {
                    text: "Lyrics ready clips: " + sunoVm.lyricsReadyCount
                    color: "#D9F1E0"
                    font.pixelSize: 14
                    font.bold: true
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Use Suno Sync for fresh lyric metadata, then play a clip. Lyrics and karaoke overlays remain handled by the C++ pipeline while this workspace streamlines navigation and playback flow."
                    color: "#9FC8AC"
                    font.pixelSize: 12
                }

                Button {
                    text: "Refresh Lyrics Data"
                    implicitHeight: 42
                    onClicked: sunoVm.refresh()
                }
            }
        }
    }
}
