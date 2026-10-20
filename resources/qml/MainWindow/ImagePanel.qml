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
            preview: imagePreview
        }

        ImagePreview {
            id: imagePreview
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingLarge
            // The block panel floats over the preview's bottom edge.
            bottomInset: blockPanel.visible
                         ? Math.max(0, blockPanel.height - Theme.spacingLarge) : 0
        }
    }

    LLOLabel {
        anchors.centerIn: parent
        visible: !Controller.hasImage
        text: qsTr("Open an image or PDF to begin")
        color: Theme.textMuted
    }

    BlockEditPanel {
        id: blockPanel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
