pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr

Button {
    id: control

    property bool emphasis: false
    property bool subtle: false

    font: Theme.caption

    background: Rectangle {
        radius: Theme.controlRadius
        color: {
            if (!control.enabled)
                return control.emphasis ? Theme.selected : Theme.surfaceSunken
            if (control.emphasis)
                return control.down ? Qt.darker(Theme.accent, 1.15) : Theme.accent
            if (control.subtle)
                return control.down ? Theme.selected
                                    : (control.hovered ? Theme.surfaceSunken
                                                       : "transparent")
            return control.down ? Theme.selected
                                : (control.hovered ? Theme.surfaceSunken
                                                   : Theme.surface)
        }
        border.color: control.emphasis && control.enabled ? Theme.accent : Theme.divider
        border.width: control.subtle && (!control.enabled || (!control.hovered && !control.down)) ? 0 : 1
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textMuted
                                : control.emphasis ? (Theme.dark ? "#1c1c1c" : "#ffffff")
                                : (control.subtle && !control.hovered
                                   ? Theme.textSecondary : Theme.textPrimary)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
