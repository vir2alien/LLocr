import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    default property alias actions: footerRow.data

    implicitHeight: footerRow.implicitHeight + 2 * Theme.spacingLarge
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.divider
    }

    RowLayout {
        id: footerRow
        anchors.fill: parent
        anchors.leftMargin: Theme.paddingWindow
        anchors.rightMargin: Theme.paddingWindow
        anchors.topMargin: Theme.spacingLarge
        anchors.bottomMargin: Theme.spacingLarge
        spacing: Theme.spacing
    }
}