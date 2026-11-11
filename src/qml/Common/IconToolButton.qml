pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// A button with a Lucide glyph before its caption; the highlight turns the
// caption into the warm accent (the «Исправить» badge). In stop mode the
// caption gives way to the stop glyph inside a spinning ring — the button stays
// enabled and its click stops the run it started. The implicit width must come
// from the content: a custom contentItem alone sizes the control to its
// padding, and the toolbar piles the buttons up. The caption sets that width in
// both modes, so the button keeps its size when it turns into the stop ring.
ToolButton {
    id: iconButton

    property string iconName: ""
    property int iconSize: 15
    property bool highlight: false
    property bool stopMode: false

    font: Theme.button
    implicitHeight: 28
    implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
    leftPadding: 5
    rightPadding: 5
    Layout.minimumWidth: implicitWidth

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: iconButton.stopMode ? stopStack.implicitHeight
                                            : contentRow.implicitHeight

        Row {
            id: contentRow
            // Not `visible`: an invisible row drops out of the layout, and
            // the width must stay the caption's in stop mode.
            opacity: iconButton.stopMode ? 0 : 1
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall

            Icon {
                visible: iconButton.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: iconButton.iconName
                size: iconButton.iconSize
                color: iconButton.highlight ? Theme.accentAttention
                      : (iconButton.enabled ? Theme.textPrimary : Theme.textMuted)
            }
            Text {
                visible: iconButton.text.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: iconButton.text
                font: iconButton.font
                color: iconButton.highlight ? Theme.accentAttention
                      : (iconButton.enabled ? Theme.textPrimary : Theme.textMuted)
            }
        }

        Item {
            id: stopStack
            visible: iconButton.stopMode
            anchors.centerIn: parent
            implicitWidth: stopRing.width
            implicitHeight: stopRing.height

            Icon {
                id: stopRing
                name: "loader-circle"
                size: Theme.iconSizeXlarge
                color: Theme.textSecondary

                RotationAnimation on rotation {
                    from: 0
                    to: 360
                    duration: 900
                    loops: Animation.Infinite
                    running: iconButton.stopMode
                }
            }

            Icon {
                anchors.centerIn: stopRing
                name: "square"
                // Even sizes keep the centering on whole pixels — an odd
                // square in an even ring lands at x.5 and renders left of the
                // ring's center.
                size: Theme.iconSizeSmall
                color: Theme.textPrimary
            }
        }
    }
}
