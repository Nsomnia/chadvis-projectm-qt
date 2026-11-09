pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import ChadVis

/**
 * Mutation-settle toast (REACTIVE-GUI-PLAN §6.2, T0359).
 *
 * One fixed sentence at a time, bottom-centre, overlaying content so it
 * never pushes or reflows it. Auto-dismisses after 4 s (success) or
 * 6 s (error, which also carries a Dismiss button).
 *
 * Queueing: at most one toast is visible and at most one waits.
 * A third arrival replaces the waiter — the oldest queued message is
 * dropped, never stacked, so a batch of mutations cannot flood the
 * screen with toasts (plan's intent; a hard cap of one waiter).
 *
 * Content is plain words only: callers pass the same fixed sentences
 * the ClipMutationsSection sheet uses, or a bridge error/update string
 * verbatim. Never pass an HTTP status or a verb enum.
 *
 * Call clear() when the route (or clipboard context) changes so a
 * stale outcome never outlives the view that invoked it.
 */
Rectangle {
    id: root

    // The sentence currently shown; "" = nothing shown.
    property string message: ""
    property bool isError: false
    property string actionText: ""

    signal actionClicked

    // One waiting message. A new arrival while one is already waiting
    // replaces it: the older queued message is dropped.
    property string pendingMessage: ""
    property bool pendingIsError: false
    property string pendingActionText: ""

    function show(nextMessage, nextIsError, nextActionText) {
        if (!nextMessage)
            return
        if (message.length === 0) {
            message = nextMessage
            isError = nextIsError
            actionText = nextActionText || ""
            dismissTimer.restart()
        } else {
            pendingMessage = nextMessage
            pendingIsError = nextIsError
            pendingActionText = nextActionText || ""
        }
    }

    function clear() {
        message = ""
        isError = false
        actionText = ""
        pendingMessage = ""
        pendingIsError = false
        pendingActionText = ""
        dismissTimer.stop()
    }

    function promoteOrHide() {
        if (pendingMessage.length > 0) {
            message = pendingMessage
            isError = pendingIsError
            actionText = pendingActionText
            pendingMessage = ""
            pendingIsError = false
            pendingActionText = ""
            dismissTimer.restart()
        } else {
            message = ""
            isError = false
            actionText = ""
        }
    }

    // Stays mounted for the fade-out.
    visible: message.length > 0 || opacity > 0.01
    opacity: message.length > 0 ? 1.0 : 0.0
    width: Math.min(toastRow.implicitWidth + Theme.spacingLarge * 2,
                    parent.width - Theme.spacingLarge * 2)
    implicitHeight: toastRow.implicitHeight + Theme.spacingLarge
    radius: Theme.radiusMedium
    color: Theme.surfaceRaised
    border.width: 1
    border.color: isError ? Theme.error : Theme.success

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }

    // Named so a screen reader announces the outcome. When the toast
    // has no Dismiss/action button it offers no focusable items at all.
    Accessible.role: Accessible.StaticText
    Accessible.name: message

    RowLayout {
        id: toastRow
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        Text {
            text: root.message
            color: root.isError ? Theme.error : Theme.textPrimary
            font: Theme.fontBody
            Layout.maximumWidth: 520
            wrapMode: Text.WordWrap
        }

        AppButton {
            visible: root.actionText.length > 0
            text: root.actionText
            flat: true
            Accessible.name: root.actionText
            onClicked: root.actionClicked()
        }

        AppButton {
            visible: root.isError
            text: "Dismiss"
            flat: true
            Accessible.name: "Dismiss " + root.message
            onClicked: {
                dismissTimer.stop()
                root.promoteOrHide()
            }
        }
    }

    Timer {
        id: dismissTimer
        interval: root.isError ? 6000 : 4000
        onTriggered: root.promoteOrHide()
    }
}
