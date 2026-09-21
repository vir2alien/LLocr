pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

Rectangle {
    color: Theme.surfaceSunken

    ImagePreview {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
    }

    LLOLabel {
        anchors.centerIn: parent
        visible: !Controller.hasImage
        text: qsTr("Open an image or PDF to begin")
        color: Theme.textMuted
    }

    VerificationPanel {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}