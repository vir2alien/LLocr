import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

Rectangle {
    color: Theme.surfaceSunken

    ImagePreview {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
    }

    Label {
        anchors.centerIn: parent
        visible: !controller.hasImage
        text: qsTr("Open an image or PDF to begin")
        color: Theme.textMuted
    }
}