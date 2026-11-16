pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr

// The radio button in the app's theme: a flat ring with a dot, the twin of
// the square of the checkbox.
RadioButton {
    id: control

    spacing: Theme.spacing
    font.pixelSize: Theme.bodySize

    implicitHeight: Theme.controlHeight
    padding: 0

    indicator: Rectangle {
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: width / 2
        color: Theme.surface
        border.color: control.checked ? Theme.accent : Theme.border
        border.width: 1
        opacity: control.enabled ? 1.0 : 0.5

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: width / 2
            color: Theme.accent
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.textMuted
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator && !control.mirrored
                     ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored
                      ? control.indicator.width + control.spacing : 0
        elide: Text.ElideRight
    }
}
