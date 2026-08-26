/**
 * @file SectionHeader.qml
 * @brief Accent-colored section header used across settings panels
 *
 * Extraction of the repeated section title style:
 * accent color + fontSubtitle + small top margin.
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import ChadVis

Text {
    color: Theme.accent
    font: Theme.fontSubtitle
    Layout.topMargin: Theme.spacingSmall
}
