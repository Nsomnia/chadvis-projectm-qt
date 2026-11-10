import QtQuick
import QtQuick.Controls
import ChadVis

/**
 * The card's overflow menu (REACTIVE-GUI-PLAN §6.2: "for clip verbs,
 * a context menu on ClipCard that opens the same controls — one menu
 * component, two entry points, not two menus").
 *
 * Entry points:
 *   - "Details & actions…" opens the clip's ClipDetailSheet, where
 *     ClipMutationsSection carries the full stateful verbs.
 *   - The single-shot verbs below (visibility, share, trash) call the same
 *     bridge invokables and report their settle on the card's own
 *     note line (ClipCard.mutationNote), filtered by clip id, so a
 *     card only ever narrates its own clip. Everything that needs
 *     a field, a picker, or a state readout routes to the sheet.
 *
 * Gate honesty matches the sheet: an entry is ABSENT when its gate
 * denies. Every entry is disabled while mutationsBusy.
 */
Menu {
    id: root

    property var clipData: null
    property string clipTitle: clipData ? (clipData.title || "Untitled") : "clip"
    property bool libraryAvailable: root.gateAvailable("library")
    property bool sharingAvailable: root.gateAvailable("sharing")

    signal openSheet(var clip)

    function gateAvailable(gateName) {
        const rows = SunoBridge.gateStatuses
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].gate === gateName)
                return rows[i].available === true
        }
        return false
    }

    MenuItem {
        text: "Details & actions…"
        enabled: !!root.clipData
        onTriggered: root.openSheet(root.clipData)
    }

    MenuSeparator {
        visible: root.libraryAvailable || root.sharingAvailable
    }

    MenuItem {
        visible: root.libraryAvailable
        text: root.clipData && root.clipData.is_public === true ? "Make private" : "Make public"
        enabled: root.libraryAvailable && !!root.clipData && !SunoBridge.mutationsBusy
        onTriggered: {
            const target = !(root.clipData.is_public === true)
            SunoBridge.setClipVisibility(root.clipData.id, target)
        }
    }

    MenuItem {
        visible: root.sharingAvailable
        text: "Get share link"
        enabled: root.sharingAvailable && !!root.clipData && !SunoBridge.mutationsBusy
        onTriggered: SunoBridge.shareClip(root.clipData.id, "clip")
    }

    MenuSeparator {
        visible: root.libraryAvailable
    }

    MenuItem {
        visible: root.libraryAvailable
        text: "Move to trash"
        enabled: root.libraryAvailable && !!root.clipData && !SunoBridge.mutationsBusy
        onTriggered: SunoBridge.setClipTrashed([root.clipData.id], true)
    }
}
