import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Image {
    id: previewImage
    fillMode: Image.PreserveAspectFit
    source: controller.hasImage
            ? "image://ocr/current?" + mainWindow.imageRevision
            : ""
    cache: false

    Item {
        id: imageArea
        width: previewImage.paintedWidth
        height: previewImage.paintedHeight
        anchors.centerIn: parent

        property int selectedBoxIndex: -1

        Connections {
            target: controller
            function onPageChanged() { imageArea.selectedBoxIndex = -1 }
            function onDocumentChanged() { imageArea.selectedBoxIndex = -1 }
            function onResultChanged() { imageArea.selectedBoxIndex = -1 }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: imageArea.selectedBoxIndex = -1
        }

        Keys.onDeletePressed: (event) => {
            if (imageArea.selectedBoxIndex >= 0
                    && controller.boxModel.isImageBox(imageArea.selectedBoxIndex)) {
                controller.boxModel.removeBox(imageArea.selectedBoxIndex)
                imageArea.selectedBoxIndex = -1
                event.accepted = true
            }
        }

        Repeater {
            model: controller.boxModel
            delegate: Rectangle {
                id: boxDelegate
                required property int index
                required property real boxX
                required property real boxY
                required property real boxWidth
                required property real boxHeight
                required property string boxText
                required property string boxLabel

                property bool isImage: boxLabel === "image" || boxLabel === "chart"

                color: isImage ? Theme.overlayImageFill : "transparent"
                border.color: isImage ? Theme.overlayImageOuter : Theme.overlayTextOuter
                border.width: 1
                x: boxX * imageArea.width
                y: boxY * imageArea.height
                width: boxWidth * imageArea.width
                height: boxHeight * imageArea.height

                HoverHandler { id: boxHover }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    color: "transparent"
                    border.color: isImage ? Theme.overlayImageInner : Theme.overlayTextInner
                    border.width: 1
                }

                Rectangle {
                    visible: boxLabel.length > 0
                    color: isImage ? Theme.overlayImageOuter : Theme.overlayTextOuter
                    radius: 2
                    height: labelText.height + 2
                    width: labelText.width + 6
                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        text: boxLabel
                        font.pixelSize: 10
                        color: isImage ? Theme.overlayImageInner : Theme.overlayTextInner
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    visible: boxDelegate.isImage
                    cursorShape: Qt.SizeAllCursor
                    acceptedButtons: Qt.LeftButton

                    property real grabX: 0
                    property real grabY: 0
                    property real origX: 0
                    property real origY: 0

                    onPressed: (mouse) => {
                        imageArea.selectedBoxIndex = boxDelegate.index
                        imageArea.forceActiveFocus()
                        origX = boxDelegate.boxX
                        origY = boxDelegate.boxY
                        const p = mapToItem(imageArea, mouse.x, mouse.y)
                        grabX = p.x
                        grabY = p.y
                    }
                    onPositionChanged: (mouse) => {
                        const p = mapToItem(imageArea, mouse.x, mouse.y)
                        const dx = (p.x - grabX) / imageArea.width
                        const dy = (p.y - grabY) / imageArea.height
                        const nx = Math.max(0, Math.min(1 - boxWidth, origX + dx))
                        const ny = Math.max(0, Math.min(1 - boxHeight, origY + dy))
                        controller.onBoxRectChanged(boxDelegate.index, nx, ny,
                                                    boxWidth, boxHeight)
                    }
                }

                ToolButton {//delete button
                    visible: boxHover.hovered && boxDelegate.isImage
                    text: "\u2715"
                    width: 18
                    height: 18
                    padding: 0
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 1
                    font.pixelSize: 10
                    onClicked: {
                        controller.boxModel.removeBox(boxDelegate.index)
                        imageArea.selectedBoxIndex = -1
                    }
                }

                // --- Image-block boundary editing (resize handles) ---
                function resizeRect(mode, ox, oy, ow, oh, dx, dy) {
                    const minS = 0.01
                    var nx = ox, ny = oy, nw = ow, nh = oh
                    if (mode === "W" || mode === "NW" || mode === "SW") {
                        nx = Math.max(0, Math.min(ox + ow - minS, ox + dx))
                        nw = ox + ow - nx
                    }
                    if (mode === "E" || mode === "NE" || mode === "SE")
                        nw = Math.max(minS, Math.min(1 - ox, ow + dx))
                    if (mode === "N" || mode === "NW" || mode === "NE") {
                        ny = Math.max(0, Math.min(oy + oh - minS, oy + dy))
                        nh = oy + oh - ny
                    }
                    if (mode === "S" || mode === "SW" || mode === "SE")
                        nh = Math.max(minS, Math.min(1 - oy, oh + dy))
                    return { x: nx, y: ny, w: nw, h: nh }
                }

                Repeater {
                    model: [
                        { mode: "NW", cursor: Qt.SizeFDiagCursor },
                        { mode: "N",  cursor: Qt.SizeVerCursor },
                        { mode: "NE", cursor: Qt.SizeBDiagCursor },
                        { mode: "E",  cursor: Qt.SizeHorCursor },
                        { mode: "SE", cursor: Qt.SizeFDiagCursor },
                        { mode: "S",  cursor: Qt.SizeVerCursor },
                        { mode: "SW", cursor: Qt.SizeBDiagCursor },
                        { mode: "W",  cursor: Qt.SizeHorCursor }
                    ]
                    delegate: Rectangle {
                        id: handle
                        required property var modelData

                        width: 10
                        height: 10
                        radius: 2
                        color: Theme.surfaceAlt
                        border.color: Theme.overlayImageOuter
                        border.width: 1
                        visible: imageArea.selectedBoxIndex === boxDelegate.index
                                 && boxDelegate.isImage

                        x: {
                            const m = modelData.mode
                            if (m === "NW" || m === "W" || m === "SW")
                                return -5
                            if (m === "N" || m === "S")
                                return (boxDelegate.width - width) / 2
                            return boxDelegate.width - 5
                        }
                        y: {
                            const m = modelData.mode
                            if (m === "NW" || m === "N" || m === "NE")
                                return -5
                            if (m === "W" || m === "E")
                                return (boxDelegate.height - height) / 2
                            return boxDelegate.height - 5
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: modelData.cursor

                            property real startX: 0
                            property real startY: 0
                            property real startW: 0
                            property real startH: 0
                            property real grabX: 0
                            property real grabY: 0

                            onPressed: (mouse) => {
                                startX = boxDelegate.boxX
                                startY = boxDelegate.boxY
                                startW = boxDelegate.boxWidth
                                startH = boxDelegate.boxHeight
                                const p = mapToItem(imageArea, mouse.x, mouse.y)
                                grabX = p.x
                                grabY = p.y
                            }
                            onPositionChanged: (mouse) => {
                                const p = mapToItem(imageArea, mouse.x, mouse.y)
                                const r = boxDelegate.resizeRect(modelData.mode,
                                    startX, startY, startW, startH,
                                    (p.x - grabX) / imageArea.width,
                                    (p.y - grabY) / imageArea.height)
                                controller.onBoxRectChanged(boxDelegate.index,
                                                            r.x, r.y, r.w, r.h)
                            }
                        }
                    }
                }
            }
        }
    }
}