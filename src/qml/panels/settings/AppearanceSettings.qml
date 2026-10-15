/**
 * @file AppearanceSettings.qml
 * @brief Theme selector and colour picker — the whole Appearance panel.
 *
 * A theme is a definition, not two loose colours. This panel has two halves and
 * they mean different things:
 *
 *   1. THEME — pick one of the built-ins. Selecting previews it live; Apply
 *      commits it. Preview and commit are separate because a theme repaints
 *      every token in the window, and a user who picks the wrong one should be
 *      able to back out without having saved a choice they did not make.
 *
 *   2. COLOUR — pick a custom accent or background. This commits immediately.
 *      It does not need a separate apply step, because the picker already has
 *      Cancel: the drag is undone by closing the popup, so a committed edit is
 *      one the user has already finished.
 *
 * Using a colour makes the active theme `custom`. That is deliberate — it is
 * what "I changed something" means — and it inherits the colours of the theme
 * it was derived from, so editing gruvbox's accent leaves gruvbox's text ramp
 * in place instead of snapping to a bare derivation.
 *
 * T0047/T0183. The panel previously held two AppTextFields whose `text` was
 * bound to `Theme.accent` — a `color` in a `string` field, which qmllint calls
 * `incompatible-type` and which yields a coerced string rather than hex. Those
 * fields were deleted rather than retyped, which left the panel with a swatch
 * and a label and no way to change anything. The hex fields below are
 * string→string (`Theme.toRgbaHex(color)` in, never `Theme.accent` into `text`),
 * and they are tracked with a Connections handler rather than a binding: a
 * TextField whose `text` is bound loses that binding the first time the user
 * types, and QML has no imperative way to put it back.
 *
 * @version 1.1.0
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    id: root
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader { text: "Appearance" }

    // ── Theme ──────────────────────────────────────────────────────────────

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.spacingMedium
        rowSpacing: Theme.spacingSmall

        Text {
            text: "Theme"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        AppComboBox {
            id: themeCombo
            Layout.fillWidth: true
            // The model is plain strings; currentIndex tracks the applied theme,
            // not the last clicked one, so a preview that is cancelled snaps the
            // combo back. -1 is a real state here: it means "none of these",
            // which is what an unknown --theme value resolves to before the
            // fallback chain runs.
            model: Theme.availableThemeIds().map(function (id) {
                return Theme.themeDisplayName(id)
            })

            onActivated: (index) => {
                // Preview only. Apply() is what commits.
                Theme.applyThemeById(Theme.availableThemeIds()[index])
                pendingThemeId = Theme.currentThemeId
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: Theme.themeDescription(Theme.currentThemeId)
        color: Theme.textDisabled
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
        visible: text.length > 0
    }

    // ── Colour ─────────────────────────────────────────────────────────────

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.spacingMedium
        rowSpacing: Theme.spacingSmall

        Text {
            text: "Accent"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        RowLayout {
            spacing: Theme.spacingSmall

            AppColorPicker {
                id: accentPicker
                Layout.preferredWidth: Theme.inputHeight
                value: Theme.accent
                onPicked: (c) => Theme.applyAccent(c)
            }

            AppTextField {
                id: accentHex
                Layout.fillWidth: true
                text: Theme.toRgbaHex(Theme.accent)
                onEditingFinished: {
                    const wanted = text.trim().toUpperCase()
                    if (wanted !== Theme.toRgbaHex(Theme.accent))
                        Theme.applyAccent(wanted)
                    else
                        text = Theme.toRgbaHex(Theme.accent)
                }
            }
        }

        Text {
            text: "Background"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        RowLayout {
            spacing: Theme.spacingSmall

            AppColorPicker {
                id: backgroundPicker
                Layout.preferredWidth: Theme.inputHeight
                value: Theme.background
                onPicked: (c) => Theme.applyBackground(c)
            }

            AppTextField {
                id: backgroundHex
                Layout.fillWidth: true
                text: Theme.toRgbaHex(Theme.background)
                onEditingFinished: {
                    const wanted = text.trim().toUpperCase()
                    if (wanted !== Theme.toRgbaHex(Theme.background))
                        Theme.applyBackground(wanted)
                    else
                        text = Theme.toRgbaHex(Theme.background)
                }
            }
        }
    }

    // ── Contrast readout ──────────────────────────────────────────────────
    //
    // T0189. This exists because the picker will happily let you set 1.4:1
    // grey text and then tell you nothing, and "why is everything grey" is a
    // bad afternoon. Two numbers, both measured live, both recomputed as the
    // theme changes -- these are bindings, so they cannot drift.
    //
    // `surfaceRaised` is the surface the labels actually sit on; using
    // `background` would flatter every number by a full step.

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMedium

        Text {
            text: "Body text"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        Text {
            text: Theme.formatContrast(Theme.textPrimary, Theme.surfaceRaised)
            color: Theme.contrastRatio(Theme.textPrimary, Theme.surfaceRaised) >= 4.5
                   ? Theme.success : Theme.warning
            font: Theme.fontCaption
        }

        Text {
            text: "Secondary"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        Text {
            text: Theme.formatContrast(Theme.textSecondary, Theme.surfaceRaised)
            color: Theme.contrastRatio(Theme.textSecondary, Theme.surfaceRaised) >= 4.5
                   ? Theme.success : Theme.warning
            font: Theme.fontCaption
        }

        Text {
            text: "Accent as focus ring"
            color: Theme.textSecondary
            font: Theme.fontCaption
        }

        Text {
            text: Theme.formatContrast(Theme.accent, Theme.surfaceRaised)
            // 3:1, not 4.5: the accent is a UI component outline (WCAG 1.4.11),
            // not body text.
            color: Theme.contrastRatio(Theme.accent, Theme.surfaceRaised) >= 3.0
                   ? Theme.success : Theme.warning
            font: Theme.fontCaption
        }

        Item { Layout.fillWidth: true }
    }

    Text {
        Layout.fillWidth: true
        text: "Contrast is measured as you change colours. Body text needs 4.5:1; "
              + "the accent, as a focus ring, needs 3:1."
        color: Theme.textDisabled
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    // ── Buttons ────────────────────────────────────────────────────────────

    RowLayout {
        spacing: Theme.spacingSmall

        AppButton {
            text: "Reset to Default"
            onClicked: {
                Theme.applyThemeById(ThemeDefinitions.defaultId)
                pendingThemeId = Theme.currentThemeId
            }
        }

        Item { Layout.fillWidth: true }

        AppButton {
            text: "Apply"
            highlighted: true
            onClicked: root.apply()
        }
    }

    Text {
        Layout.fillWidth: true
        text: SettingsBridge.sessionTheme.length > 0
              ? "--theme " + SettingsBridge.sessionTheme + " is active for this session. Saving here "
                + "stores a theme that will apply on the next launch."
              : "Changes are saved when you apply them."
        color: Theme.textDisabled
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    /**
     * The theme id to commit. Empty means "whatever is applied right now", so a
     * panel that was never touched does not rewrite the stored value on Apply.
     */
    property string pendingThemeId: ""

    /**
     * Commit. Writes the theme id only — the colours are already persisted by
     * Theme.commit(), which every apply path goes through.
     */
    function apply() {
        SettingsBridge.themeName = pendingThemeId.length > 0
                                   ? pendingThemeId
                                   : Theme.currentThemeId
    }

    /**
     * The combo follows the applied theme. `currentIndex` is set imperatively
     * from the id rather than bound to a computed index, because the model is
     * display names and Theme reports ids — a binding between the two would need
     * a lookup that silently does nothing when the id is not in the list.
     */
    Component.onCompleted: {
        pendingThemeId = Theme.currentThemeId
        syncThemeCombo()
    }

    function syncThemeCombo() {
        const ids = Theme.availableThemeIds()
        const index = ids.indexOf(Theme.currentThemeId)
        if (index >= 0)
            themeCombo.currentIndex = index
    }

    Connections {
        target: Theme
        function onThemeChanged() {
            accentHex.text = Theme.toRgbaHex(Theme.accent)
            backgroundHex.text = Theme.toRgbaHex(Theme.background)
            accentPicker.value = Theme.accent
            backgroundPicker.value = Theme.background
        }
        function onCurrentThemeIdChanged() {
            root.syncThemeCombo()
        }
    }
}