import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ToolBar {
    leftPadding: Theme.spacing * 2
    rightPadding: Theme.spacing * 2
    height: Theme.controlHeight

    background: Rectangle {
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.divider
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Label {
            text: controller.statusMessage
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        BusyIndicator {
            running: controller.busy
            visible: controller.busy
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
        }
    }
}