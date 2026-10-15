pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

Rectangle {
    id: imagePanelRoot

    color: Theme.surfaceSunken

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageToolBar {
            Layout.fillWidth: true
        }

        ImagePreview {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingLarge
        }
    }

    LLOLabel {
        anchors.centerIn: parent
        visible: !Controller.hasImage
        text: qsTr("Open an image or PDF to begin")
        color: Theme.textMuted
    }

    BlockEditPanel {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
