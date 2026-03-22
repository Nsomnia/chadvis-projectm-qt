import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

ColumnLayout {
    id: root

    property string searchQuery: ""
    property bool showSearch: false

    spacing: 0

    ToolBar {
        Layout.fillWidth: true
        visible: LyricsBridge.hasLyrics
        background: Rectangle { color: Theme.surfaceVariant }

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Label {
                text: LyricsBridge.title + (LyricsBridge.artist ? " - " + LyricsBridge.artist : "")
                color: Theme.onSurface
                font.pixelSize: Theme.fontSizeMedium
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            ToolButton {
                text: "\u{1F50D}"
                onClicked: root.showSearch = !root.showSearch
            }

            ToolButton {
                text: "SRT"
                onClicked: exportDialog.open()
            }
        }
    }

    TextField {
        id: searchField
        Layout.fillWidth: true
        Layout.margins: Theme.spacingSmall
        visible: root.showSearch && LyricsBridge.hasLyrics
        placeholderText: "Search lyrics..."
        text: root.searchQuery
        onTextChanged: root.searchQuery = text

        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surface
            border.color: searchField.activeFocus ? Theme.primary : Theme.outline
        }
    }

    ListView {
        id: lyricsList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        model: root.searchQuery.length > 0 ? LyricsBridge.searchResults : LyricsBridge.lines
        delegate: LyricsLineDelegate {
            width: lyricsList.width
            isCurrentLine: modelData.index === LyricsBridge.currentLineIndex
            lineProgress: modelData.index === LyricsBridge.currentLineIndex ? LyricsBridge.lineProgress : 0
            onClicked: LyricsBridge.seekToLine(modelData.index)
        }

        highlight: Rectangle {
            color: Theme.primary
            opacity: 0.15
            radius: Theme.radiusSmall
        }
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 200
        highlightResizeDuration: 0

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        Connections {
            target: LyricsBridge
            function onPositionChanged() {
                if (LyricsBridge.currentLineIndex >= 0) {
                    lyricsList.currentIndex = LyricsBridge.currentLineIndex
                    lyricsList.positionViewAtIndex(LyricsBridge.currentLineIndex, ListView.Center)
                }
            }
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: !LyricsBridge.hasLyrics
        text: "No lyrics loaded"
        color: Theme.onSurfaceVariant
        font.pixelSize: Theme.fontSizeLarge
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Dialog {
        id: exportDialog
        title: "Export Lyrics"
        standardButtons: Dialog.Save | Dialog.Cancel

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingMedium

            Label {
                text: "Export format:"
            }

            RadioButton {
                id: srtRadio
                text: "SRT (Subtitles)"
                checked: true
            }

            RadioButton {
                id: lrcRadio
                text: "LRC (Lyrics)"
            }

            TextField {
                id: pathField
                Layout.fillWidth: true
                placeholderText: "Output path..."
                text: "/tmp/lyrics." + (srtRadio.checked ? "srt" : "lrc")
            }
        }

        onAccepted: {
            if (srtRadio.checked) {
                LyricsBridge.exportToSrt(pathField.text)
            } else {
                LyricsBridge.exportToLrc(pathField.text)
            }
        }
    }

    component LyricsLineDelegate: Rectangle {
        id: delegate
        height: lineText.implicitHeight + Theme.spacingMedium
        color: mouseArea.containsMouse ? Theme.surfaceVariant : Theme.surface
        radius: Theme.radiusSmall

        property bool isCurrentLine: false
        property real lineProgress: 0
        property bool isInstrumental: modelData ? modelData.isInstrumental : false

        signal clicked()

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Rectangle {
                width: 4
                height: parent.height
                radius: 2
                color: delegate.isCurrentLine ? Theme.accent : "transparent"
                visible: delegate.isCurrentLine
            }

            Text {
                id: lineText
                text: modelData ? modelData.text : ""
                color: delegate.isCurrentLine ? Theme.primary : Theme.onSurface
                font.pixelSize: delegate.isCurrentLine ? Theme.fontSizeLarge : Theme.fontSizeMedium
                font.bold: delegate.isCurrentLine
                font.italic: delegate.isInstrumental
                elide: Text.ElideRight
                Layout.fillWidth: true

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: delegate.lineProgress * parent.width
                    color: Theme.accent
                    opacity: 0.3
                    visible: delegate.lineProgress > 0
                }
            }

	Text {
		text: delegate.isInstrumental ? "♪" : ""
		color: Theme.onSurfaceVariant
		font.pixelSize: Theme.fontSizeMedium
		visible: text !== ""
	}
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: delegate.clicked()
        }
    }
}
