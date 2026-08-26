/**
 * @file AppTextField.qml
 * @brief Themed TextField used across all panels
 *
 * Provides the standard raised-surface chrome with accent border on focus.
 * All other TextField properties (text, placeholderText, echoMode, color,
 * font, padding...) remain settable per-instance exactly as before.
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Controls
import ChadVis

TextField {
    id: root

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusSmall
        border.color: root.activeFocus ? Theme.accent : Theme.border
    }
}
