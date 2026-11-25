/**
 * @file LineageView.qml
 * @brief The song-lineage constellation: what this clip came from, what came from it.
 *
 * Layout is done in C++ (`vc::suno::lineage::buildGraph`) so this file only draws.
 * One column per generation, older generations to the left, and the focus clip in
 * the middle. Edges are bezier curves on a Canvas: solid and accent-coloured where
 * Suno's own attribution named the parent, dashed and faded where ChadVis inferred
 * it from text.
 *
 * The tuning panel is here rather than in the Settings window because these are
 * dials on THIS picture: a weight is only meaningful while you can see the graph
 * change. Values persist in the library cache (`lineage_meta`), so they survive a
 * restart without a schema change to `config.toml`.
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

Item {
    id: root

    readonly property int nodeWidth: 208
    readonly property int nodeHeight: 76
    readonly property int columnGap: 272
    readonly property int rowGap: 96
    readonly property int margin: 64

    property bool tuningOpen: false
    property real zoom: 1.0
    /// id -> {x, y}, rebuilt whenever the node list changes. Kept as a map so the
    /// Canvas never scans the model per edge.
    property var nodePositions: ({})
    property var signalNames: ["title", "style", "prompt", "language", "model", "age"]

    function signalLabel(key) {
        switch (key) {
        case "title":    return "Title overlap"
        case "style":    return "Style / genre tags"
        case "prompt":   return "Description wording"
        case "language": return "Same script"
        case "model":    return "Model generation"
        case "age":      return "Creation proximity"
        default:         return key
        }
    }

    function rebuildPositions() {
        var nodes = SunoLineageBridge.nodes
        var map = ({})
        for (var i = 0; i < nodes.length; ++i) {
            var node = nodes[i]
            map[node.id] = { x: node.column * columnGap + margin,
                             y: node.row * rowGap + margin }
        }
        nodePositions = map
        edgeCanvas.requestPaint()
    }

    function resetView() {
        zoom = 1.0
        world.x = 0
        world.y = 0
        edgeCanvas.requestPaint()
    }

    // A new graph is always shown from the top-left: leaving the previous pan in
    // place would drop the user into empty canvas with a different library.
    Connections {
        target: SunoLineageBridge

        function onGraphChanged() {
            root.zoom = Math.min(Math.max(root.zoom, 0.5), 1.5)
            world.x = 0
            world.y = 0
            root.rebuildPositions()
        }
    }

    Component.onCompleted: rebuildPositions()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ─────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.surface

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingSmall

                ColumnLayout {
                    spacing: 0

                    Text {
                        text: SunoLineageBridge.focusTitle.length > 0
                              ? SunoLineageBridge.focusTitle
                              : "Song lineage"
                        color: Theme.textPrimary
                        font: Theme.fontSubtitle
                        elide: Text.ElideRight
                        Layout.maximumWidth: 380
                    }

                    Text {
                        text: {
                            var parts = []
                            if (SunoLineageBridge.focusId.length === 0)
                                parts.push("pick a song to begin")
                            else
                                parts.push(SunoLineageBridge.ancestorCount + " ancestor(s)",
                                           SunoLineageBridge.descendantCount + " descendant(s)")
                            parts.push(SunoLineageBridge.inferredEdgeCount + " inferred link(s)")
                            if (SunoLineageBridge.remoteEdgeCount > 0)
                                parts.push(SunoLineageBridge.remoteEdgeCount + " from the server")
                            return parts.join(" · ")
                        }
                        color: Theme.textSecondary
                        font: Theme.fontTiny
                        elide: Text.ElideRight
                        Layout.maximumWidth: 520
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Rebuild"
                    highlighted: SunoLineageBridge.inferredEdgeCount === 0
                    onClicked: SunoLineageBridge.rebuild()
                    Accessible.name: "Rebuild the inferred lineage from the cached library"
                }

                AppButton {
                    text: "Server ancestry"
                    enabled: SunoLineageBridge.focusId.length > 0 && !SunoLineageBridge.busy
                    onClicked: SunoLineageBridge.fetchRemoteAncestry(SunoLineageBridge.focusId)
                    Accessible.name: "Fetch this clip's attribution from Suno"
                }

                AppButton {
                    text: root.tuningOpen ? "Hide tuning" : "Tuning"
                    flat: true
                    onClicked: root.tuningOpen = !root.tuningOpen
                    Accessible.name: "Show or hide the lineage tuning panel"
                }

                AppButton {
                    text: "Fit"
                    flat: true
                    onClicked: root.resetView()
                    Accessible.name: "Reset zoom and pan"
                }
            }
        }

        // ── Graph + tuning ─────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                id: graphArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Rectangle {
                    anchors.fill: parent
                    color: Theme.background
                }

                Item {
                    id: world
                    width: Math.max(graphArea.width,
                                    SunoLineageBridge.columns * root.columnGap + root.margin * 2)
                    height: Math.max(graphArea.height,
                                     SunoLineageBridge.rows * root.rowGap + root.margin * 2)
                    transformOrigin: Item.TopLeft
                    scale: root.zoom

                    Canvas {
                        id: edgeCanvas
                        anchors.fill: parent

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var edges = SunoLineageBridge.edges
                            var positions = root.nodePositions
                            ctx.lineWidth = 2
                            for (var i = 0; i < edges.length; ++i) {
                                var edge = edges[i]
                                var child = positions[edge.from]
                                var parent = positions[edge.to]
                                if (!child || !parent)
                                    continue
                                var startX = child.x
                                var startY = child.y + root.nodeHeight / 2
                                var endX = parent.x + root.nodeWidth
                                var endY = parent.y + root.nodeHeight / 2
                                var midX = (startX + endX) / 2

                                var remote = edge.provenance === "remote"
                                ctx.strokeStyle = remote
                                        ? Theme.accent
                                        : Theme.withAlpha(Theme.accent,
                                                          0.25 + 0.55 * Math.min(1, edge.confidence))
                                ctx.setLineDash(remote ? [] : [6, 6])
                                ctx.beginPath()
                                ctx.moveTo(startX, startY)
                                ctx.bezierCurveTo(midX, startY, midX, endY, endX, endY)
                                ctx.stroke()
                            }
                            ctx.setLineDash([])
                        }
                    }

                    Repeater {
                        model: SunoLineageBridge.nodes

                        delegate: LineageNode {
                            required property var modelData

                            x: root.nodePositions[modelData.id]
                               ? root.nodePositions[modelData.id].x : 0
                            y: root.nodePositions[modelData.id]
                               ? root.nodePositions[modelData.id].y : 0
                            title: modelData.title.length > 0 ? modelData.title : modelData.id
                            subtitle: modelData.tags
                            badge: modelData.role === "focus"
                                   ? "this song"
                                   : (modelData.relation !== "unknown"
                                      ? modelData.relation : modelData.provenance)
                            confidence: modelData.confidence
                            inferred: modelData.provenance !== "remote"
                            current: modelData.role === "focus"
                            onActivated: SunoLineageBridge.focusClip(modelData.id)
                        }
                    }

                    DragHandler {
                        id: panHandler
                        target: null
                        property real startX: 0
                        property real startY: 0

                        onActiveChanged: {
                            if (active) {
                                startX = world.x
                                startY = world.y
                            }
                        }
                        onTranslationChanged: {
                            world.x = startX + translation.x
                            world.y = startY + translation.y
                        }
                    }

                    WheelHandler {
                        // Zoom is about the picture, not the page: no modifier needed,
                        // and the graph is inside a clipped viewport so the view does
                        // not scroll behind it.
                        onWheel: function(event) {
                            var factor = event.angleDelta.y > 0 ? 1.12 : 0.89
                            root.zoom = Math.max(0.35, Math.min(2.5, root.zoom * factor))
                        }
                    }
                }

                // ── Empty state ────────────────────────────────────────────
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - Theme.spacingXL * 2, 520)
                    spacing: Theme.spacingSmall
                    visible: SunoLineageBridge.nodes.length === 0

                    Text {
                        Layout.fillWidth: true
                        text: "No lineage graph yet"
                        color: Theme.textPrimary
                        font: Theme.fontSubtitle
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Press Rebuild to read the cached library and link the songs that "
                              + "share a title, a style and a description. Links inferred from "
                              + "text are guesses and are drawn dashed; press Server ancestry for "
                              + "the parents Suno itself recorded."
                        color: Theme.textSecondary
                        font: Theme.fontCaption
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // ── Status line ────────────────────────────────────────────
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: statusColumn.implicitHeight + Theme.spacingSmall * 2
                    color: Theme.withAlpha(Theme.surface, 0.92)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 1
                        color: Theme.border
                    }

                    ColumnLayout {
                        id: statusColumn
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: SunoLineageBridge.status
                            color: Theme.textSecondary
                            font: Theme.fontTiny
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: SunoLineageBridge.error.length > 0
                            text: SunoLineageBridge.error
                            color: Theme.warning
                            font: Theme.fontTiny
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: SunoLineageBridge.truncated
                            text: "The neighbourhood is cut at the display limit · "
                                  + SunoLineageBridge.hiddenNeighbours
                                  + " further related clip(s) exist · lower the depth to browse"
                            color: Theme.textDisabled
                            font: Theme.fontTiny
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // ── Tuning panel ───────────────────────────────────────────────
            Rectangle {
                id: tuningPanel
                Layout.preferredWidth: root.tuningOpen ? 320 : 0
                Layout.fillHeight: true
                visible: root.tuningOpen
                color: Theme.surface

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: Theme.border
                }

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    clip: true

                    ColumnLayout {
                        width: Math.max(0, tuningPanel.width - Theme.spacingMedium * 2)
                        spacing: Theme.spacingSmall

                        Text {
                            text: "How links are inferred"
                            color: Theme.textPrimary
                            font: Theme.fontBodyStrong
                        }

                        Text {
                            text: "Weights are relative: only their ratios matter. Changes are "
                                  + "saved immediately and take effect on the next rebuild."
                            color: Theme.textSecondary
                            font: Theme.fontTiny
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        Repeater {
                            model: root.signalNames

                            delegate: ColumnLayout {
                                required property string modelData
                                Layout.fillWidth: true
                                spacing: 0

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: root.signalLabel(modelData)
                                        color: Theme.textSecondary
                                        font: Theme.fontTiny
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: (SunoLineageBridge.weights[modelData] !== undefined
                                               ? SunoLineageBridge.weights[modelData] : 0).toFixed(2)
                                        color: Theme.textPrimary
                                        font: Theme.fontTiny
                                    }
                                }

                                Slider {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 1
                                    stepSize: 0.01
                                    value: SunoLineageBridge.weights[modelData] !== undefined
                                           ? SunoLineageBridge.weights[modelData] : 0
                                    onMoved: SunoLineageBridge.setWeight(modelData, value)
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: Theme.border
                        }

                        Text {
                            text: "Minimum confidence · " + SunoLineageBridge.minConfidencePercent + "%"
                            color: Theme.textSecondary
                            font: Theme.fontTiny
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            stepSize: 1
                            value: SunoLineageBridge.minConfidencePercent
                            onMoved: SunoLineageBridge.minConfidencePercent = Math.round(value)
                        }

                        Text {
                            text: "Ancestor depth · " + SunoLineageBridge.ancestorDepth
                            color: Theme.textSecondary
                            font: Theme.fontTiny
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 1
                            to: 8
                            stepSize: 1
                            value: SunoLineageBridge.ancestorDepth
                            onMoved: SunoLineageBridge.ancestorDepth = Math.round(value)
                        }

                        Text {
                            text: "Descendant depth · " + SunoLineageBridge.descendantDepth
                            color: Theme.textSecondary
                            font: Theme.fontTiny
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 1
                            to: 8
                            stepSize: 1
                            value: SunoLineageBridge.descendantDepth
                            onMoved: SunoLineageBridge.descendantDepth = Math.round(value)
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: Theme.border
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            AppButton {
                                text: "Reset"
                                flat: true
                                onClicked: SunoLineageBridge.resetWeights()
                            }

                            AppButton {
                                text: "Clear inferred"
                                flat: true
                                onClicked: SunoLineageBridge.clearInferred()
                            }
                        }
                    }
                }
            }
        }
    }
}
