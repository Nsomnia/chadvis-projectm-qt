/**
 * @file LibraryView.qml
 * @brief Default landing surface: the remote Suno library
 *
 * Browsing-first grid of clip cards over feed/v3 data:
 * - Server-side debounced search wired to SunoBridge.searchLibrary()
 *   (the bridge coalesces keystrokes at 350 ms)
 * - Infinite scroll via requestNextLibraryPage(), suspended while a
 *   search is active
 * - Card click opens ClipDetailSheet for full metadata
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

Item {
    id: root

    // Query lives here (not on the field) so sibling bindings that appear
    // earlier in the document never reference a not-yet-created id.
    property string query: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ═════════════════════════════════════════════
        // HEADER
        // ═════════════════════════════════════════════
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            Layout.bottomMargin: Theme.spacingMedium
            spacing: Theme.spacingMedium

            ColumnLayout {
                spacing: 2

                Text {
                    text: "Library"
                    color: Theme.textPrimary
                    font: Theme.fontDisplay
                }

                Text {
                    text: {
                        const n = SunoBridge.clips.length
                        const scope = root.query.length > 0 ? "matching “" + root.query + "”" : "in your collection"
                        return n > 0 ? n + " track" + (n === 1 ? "" : "s") + " " + scope : "Your Suno collection"
                    }
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                }
            }

            Item { Layout.fillWidth: true }

            AppTextField {
                id: searchField
                Layout.preferredWidth: 280
                placeholderText: "Search titles & styles…"
                color: Theme.textPrimary
                font: Theme.fontBody
                text: root.query

                // Bridge debounces 350 ms before hitting feed/v3; an empty
                // string resets back to the unfiltered first page.
                onTextChanged: {
                    root.query = text
                    SunoBridge.searchLibrary(text)
                }

                Image {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    source: searchField.text.length > 0 ? "qrc:/qt/qml/ChadVis/resources/icons/clear.svg"
                                                        : ""
                    visible: source !== ""
                    sourceSize: Qt.size(16, 16)
                    width: 16
                    height: 16
                    fillMode: Image.PreserveAspectFit
                    opacity: clearMouse.containsMouse ? 1.0 : 0.6

                    MouseArea {
                        id: clearMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: searchField.text = ""
                    }
                }
            }

            AppButton {
                icon: "qrc:/qt/qml/ChadVis/resources/icons/expand.svg"
                flat: true
                implicitWidth: 40
                implicitHeight: 40
                rotation: SunoBridge.loading ? 360 : 0
                Behavior on rotation {
                    SequentialAnimation {
                        NumberAnimation { duration: 900; easing.type: Easing.InOutCubic }
                    }
                }
                ToolTip.visible: hovered
                ToolTip.text: "Refresh library"
                ToolTip.delay: 400
                onClicked: SunoBridge.refreshLibrary(1)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        // ═════════════════════════════════════════════
        // CLIP GRID
        // ═════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: clipGrid
                anchors.fill: parent
                anchors.margins: Theme.spacingLarge
                clip: true

                model: SunoBridge.clips

                readonly property int columns: Math.max(2, Math.floor(width / Theme.cardTileMinimum))
                cellWidth: Math.floor(width / columns)
                cellHeight: cellWidth * 1.22 + Theme.spacingSmall

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: ClipCard {
                    width: clipGrid.cellWidth - Theme.spacingSmall
                    height: clipGrid.cellHeight - Theme.spacingSmall
                    clipData: modelData

                    // Center tile inside its cell
                    transform: Translate {
                        x: (clipGrid.cellWidth - Theme.spacingSmall - width) / 2
                        y: (clipGrid.cellHeight - Theme.spacingSmall - height) / 2
                    }

                    onOpened: function(openedClip) {
                        detailSheet.clipData = openedClip
                        detailSheet.open()
                    }
                }

                // Infinite scroll — suspended during active search so results
                // stay scoped to the query's pages.
                onAtYEndChanged: {
                    if (atYEnd && SunoBridge.hasMorePages && !SunoBridge.loading
                            && root.query === "") {
                        SunoBridge.requestNextLibraryPage()
                    }
                }

                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.durationNormal }
                    NumberAnimation { property: "scale"; from: 0.92; to: 1.0; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
                }

                // ── Empty state ─────────────────────
                Text {
                    anchors.centerIn: parent
                    visible: clipGrid.count === 0 && !SunoBridge.loading
                    text: root.query.length > 0
                          ? "No tracks match your search"
                          : "Library is empty.\nSign in via Settings → Suno AI, then refresh."
                    color: Theme.textDisabled
                    font: Theme.fontBody
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                // ── Initial load spinner ────────────
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: clipGrid.count === 0 && SunoBridge.loading
                    spacing: Theme.spacingSmall

                    BusyIndicator { Layout.alignment: Qt.AlignHCenter }
                    Text {
                        text: "Fetching your library…"
                        color: Theme.textSecondary
                        font: Theme.fontCaption
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }

        // ═════════════════════════════════════════════
        // FOOTER STRIP (pagination state)
        // ═════════════════════════════════════════════
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            color: Qt.lighter(Theme.backgroundAlt, 1.03)

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

                Text {
                    text: SunoBridge.loading ? "Loading…" :
                          (SunoBridge.hasMorePages ? "Scroll for more · page " + SunoBridge.currentPage
                                                   : "End of library")
                    color: Theme.textDisabled
                    font: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                BusyIndicator {
                    visible: SunoBridge.loading && clipGrid.count > 0
                    running: visible
                    implicitHeight: 18
                    implicitWidth: 18
                }
            }
        }
    }

    ClipDetailSheet {
        id: detailSheet
    }

    // Landing fetch: populate the default view if the session started cold
    Component.onCompleted: {
        if (!SunoBridge.loading && SunoBridge.clips.length === 0)
            SunoBridge.refreshLibrary(1)
    }
}
