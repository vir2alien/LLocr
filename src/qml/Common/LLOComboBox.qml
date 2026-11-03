pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr

// The flat dropdown. The Fusion default paints a 3D button panel and a
// shadowed popup — the app's surfaces are flat, so the control keeps the
// Fusion behaviour (popup, model, keyboard) and repaints itself in the
// app's theme.
ComboBox {
    id: control

    implicitHeight: Theme.controlHeight
    font.pointSize: Theme.bodySmallSize

    padding: 0
    leftPadding: Theme.spacing
    rightPadding: Theme.spacing + 14

    background: Rectangle {
        implicitWidth: 120
        radius: Theme.controlRadius
        color: control.down ? Theme.selected
                            : (control.hovered ? Theme.surfaceSunken : Theme.surfaceAlt)
        border.color: control.visualFocus ? Theme.cardAccent : Theme.border
        border.width: 1
    }

    contentItem: Text {
        text: control.displayText
        color: control.enabled ? Theme.textPrimary : Theme.textMuted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Icon {
        x: control.width - width - Theme.spacing
        y: (control.height - height) / 2
        name: "chevron-down"
        size: 12
        color: control.enabled ? Theme.textSecondary : Theme.textMuted
    }

    delegate: ItemDelegate {
        id: entry

        required property int index

        width: ListView.view.width
        implicitHeight: Theme.controlHeight
        padding: 0
        leftPadding: Theme.spacing
        rightPadding: Theme.spacing

        text: control.textAt(index)
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled

        background: Rectangle {
            color: entry.highlighted ? Theme.selected
                                     : (entry.hovered ? Theme.surfaceSunken : "transparent")
        }

        contentItem: Text {
            text: entry.text
            font: entry.font
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        height: Math.min(contentItem.implicitHeight + padding * 2,
                          Overlay.overlay.height - topMargin - bottomMargin)
        topMargin: 6
        bottomMargin: 6
        padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0

            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            radius: Theme.controlRadius
            color: Theme.surfaceAlt
            border.color: Theme.border
            border.width: 1
        }
    }
}
