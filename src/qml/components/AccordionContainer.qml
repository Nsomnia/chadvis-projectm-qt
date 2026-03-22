/**
 * @file AccordionContainer.qml
 * @brief Container managing multiple accordion panels
 *
 * Features:
 * - Single-expansion mode (classic accordion behavior)
 * - Animated panel transitions
 * - Panel registration and state management
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import ChadVis

ColumnLayout {
    id: root

    // ═══════════════════════════════════════════════════════════
    // PROPERTIES
    // ═══════════════════════════════════════════════════════════

    property string expandedPanel: ""
    property int animationDuration: Theme.durationNormal

    // Panel definitions: [{id, title, icon, component}]
    property var panels: []

    // ═══════════════════════════════════════════════════════════
    // SIGNALS
    // ═══════════════════════════════════════════════════════════

    signal panelChanged(string panelId)

    // ═══════════════════════════════════════════════════════════
    // FUNCTIONS
    // ═══════════════════════════════════════════════════════════

    function togglePanel(panelId) {
        if (expandedPanel === panelId) {
            expandedPanel = ""  // Collapse all
        } else {
            expandedPanel = panelId
        }
        panelChanged(panelId)
    }

    function expandPanel(panelId) {
        if (expandedPanel !== panelId) {
            expandedPanel = panelId
            panelChanged(panelId)
        }
    }

    function collapseAll() {
        if (expandedPanel !== "") {
            expandedPanel = ""
            panelChanged("")
        }
    }

    // ═══════════════════════════════════════════════════════════
    // CONTENT
    // ═══════════════════════════════════════════════════════════

    spacing: Theme.spacingTiny

    Repeater {
        model: root.panels

        delegate: AccordionPanel {
            id: panelDelegate

            Layout.fillWidth: true

            panelId: modelData.id
            title: modelData.title
            icon: modelData.icon || ""
            isExpanded: root.expandedPanel === modelData.id
            expandedHeight: modelData.expandedHeight || 250

            contentComponent: modelData.component

            onHeaderClicked: root.togglePanel(panelId)
        }
    }

    // Spacer to push panels to top if not enough content
    Item {
        Layout.fillHeight: true
    }
}
