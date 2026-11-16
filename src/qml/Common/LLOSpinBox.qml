pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr

// The flat number field. The Fusion default paints a 3D panel with its own
// arrows; the app's surfaces are flat, so the control keeps the Fusion
// behaviour (editing, stepping, keyboard) and repaints itself in the app's
// theme.
SpinBox {
    id: control

    implicitHeight: Theme.controlHeight
    implicitWidth: 90
    font.pixelSize: Theme.bodySize

    padding: 0
    leftPadding: Theme.spacing
    rightPadding: Theme.spacing + 20

    background: Rectangle {
        radius: Theme.controlRadius
        color: control.down ? Theme.selected
                            : (control.hovered ? Theme.surfaceSunken : Theme.surfaceAlt)
        border.color: control.visualFocus ? Theme.cardAccent : Theme.border
        border.width: 1
    }

    contentItem: TextInput {
        z: 2
        text: control.textFromValue(control.value, control.locale)
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.textMuted
        selectionColor: Theme.selected
        selectedTextColor: Theme.textPrimary
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    // The stepper: two chevrons on the right of the field. The Fusion
    // indicators are plain rectangles that only paint the arrows — the click
    // has to be wired here.
    component StepperArrow: Item {
        id: arrow

        required property SpinBox box
        property bool up: true

        width: Theme.iconSizeMedium
        height: box.height / 2
        x: box.width - width - Theme.spacingSmall
        y: up ? 0 : box.height / 2

        enabled: box.enabled && (up ? box.value < box.to : box.value > box.from)

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: arrow.up ? arrow.box.increase() : arrow.box.decrease()
        }

        Icon {
            anchors.centerIn: parent
            name: arrow.up ? "chevron-up" : "chevron-down"
            size: Theme.iconSizeSmall
            color: arrow.enabled ? Theme.textSecondary : Theme.textMuted
        }
    }

    up.indicator: StepperArrow {
        box: control
        up: true
    }

    down.indicator: StepperArrow {
        box: control
        up: false
    }
}
