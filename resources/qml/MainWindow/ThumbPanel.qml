import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

Rectangle {
    LLOLabel {
        anchors.centerIn: parent
        visible: controller.hasImage && controller.pageCount === 0
        text: qsTr("No pages")
        color: Theme.textMuted
    }

    ListView {
        id: thumbList
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        cacheBuffer: 10000

        interactive: draggedIndex === -1

        model: controller.pageModel

        property int draggedIndex: -1

        displaced: Transition {
            NumberAnimation { property: "y"; duration: 150; easing.type: Easing.OutQuad }
        }

        delegate: ThumbDelegate {
            width: thumbList.width - Theme.spacingSmall
            height: width * 1.3 + 22
        }
    }//ListView
}