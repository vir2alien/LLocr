pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr
import "../Common"

// The section list on the left of the settings window (ADR 297). The window
// owns the sections; the nav only reports the pick.
ListView {
    id: root

    required property var sections
    signal sectionPicked(string sectionId)

    implicitWidth: 200
    clip: true
    currentIndex: 0
    spacing: 0
    model: root.sections
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    readonly property string currentSectionId: {
        if (root.currentIndex < 0 || root.currentIndex >= root.sections.length)
            return ""
        return root.sections[root.currentIndex].id
    }

    readonly property string currentSectionTitle: {
        if (root.currentIndex < 0 || root.currentIndex >= root.sections.length)
            return ""
        return root.sections[root.currentIndex].title
    }

    delegate: Item {
        id: row

        required property var modelData
        required property int index

        width: ListView.view.width
        height: Theme.rowHeightLarge

        Rectangle {
            anchors.fill: parent
            radius: Theme.controlRadius
            color: row.index === root.currentIndex ? Theme.selected
                 : (rowHover.hovered ? Theme.surfaceSunken : "transparent")
        }

        Icon {
            id: rowIcon

            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            name: row.modelData.icon
            size: Theme.iconSizeMedium
            color: row.index === root.currentIndex ? Theme.textPrimary : Theme.textSecondary
        }

        LLOLabel {
            anchors.left: rowIcon.right
            anchors.leftMargin: Theme.spacingSmall
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            text: row.modelData.title
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            font.pixelSize: Theme.bodySize
            color: row.index === root.currentIndex ? Theme.textPrimary : Theme.textSecondary
        }

        HoverHandler { id: rowHover }

        TapHandler {
            onTapped: {
                root.currentIndex = row.index
                root.sectionPicked(row.modelData.id)
            }
        }
    }
}
