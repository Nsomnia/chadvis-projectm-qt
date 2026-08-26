/**
 * @file AppComboBox.qml
 * @brief Themed ComboBox chrome (raised background + caption display text)
 *
 * Matches the previously-inlined ComboBox styling in SettingsPanel and
 * RecordingPanel: surfaceRaised/radiusSmall background with Theme.border,
 * plus a vertical-center Text contentItem showing the display text.
 *
 * Per-instance overrides:
 * - contentFont: font of the display text (default Theme.fontCaption)
 * - contentLeftPadding: left inset of the display text (default 8)
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Controls
import ChadVis

ComboBox {
    id: root

    property font contentFont: Theme.fontCaption
    property int contentLeftPadding: 8

    contentItem: Text {
        text: root.displayText
        color: Theme.textPrimary
        font: root.contentFont
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.contentLeftPadding
    }

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusSmall
        border.color: Theme.border
    }
}
