pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr

CheckBox {
    id: control

    spacing: 10
    font.pointSize: Theme.bodySmallSize

    indicator: Rectangle {
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: Theme.controlRadius
        color: control.checked ? Theme.accent : Theme.surface
        border.color: control.checked ? Theme.accent : Theme.border
        border.width: 1
        opacity: control.enabled ? 1.0 : 0.5

        Text {
            anchors.centerIn: parent
            text: "\u2713"
            visible: control.checked
            font.pointSize: Theme.footnoteSize
            font.bold: true
            color: Theme.dark ? "#1c1c1c" : "#ffffff"
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
