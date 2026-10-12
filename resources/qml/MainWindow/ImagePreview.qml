pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Image {
    id: previewImage
    fillMode: Image.PreserveAspectFit
    source: Controller.hasImage
            ? "image://ocr/current?" + Controller.imageRevision
            : ""
    cache: false

    LLOLabel {
        anchors.centerIn: parent
        visible: Controller.previewRenderingPage === Controller.currentPage
        text: qsTr("Rendering page…")
        font.pointSize: Theme.captionSize
        color: Theme.textMuted
        background: Rectangle {
            color: Theme.surface
            radius: Theme.radius
            border.color: Theme.border
            border.width: 1
        }
        padding: 6
    }

    LLOLabel {
        anchors.centerIn: parent
        visible: previewImage.status === Image.Error
        text: qsTr("Could not load the page image")
        font.pointSize: Theme.captionSize
        color: Theme.error
        background: Rectangle {
            color: Theme.surface
            radius: Theme.radius
            border.color: Theme.error
            border.width: 1
        }
        padding: 6
    }

    Item {
        id: imageArea
        width: previewImage.paintedWidth
        height: previewImage.paintedHeight
        anchors.centerIn: parent

        MouseArea {
            anchors.fill: parent
            onClicked: Controller.selectedBoxIndex = -1
        }

        Keys.onDeletePressed: (event) => {
            const index = Controller.selectedBoxIndex
            if (index >= 0 && !Controller.checkBusy && Controller.boxModel.isImageBox(index)
                    && Controller.removeBlock(index)) {
                // removeBlock() clears the selection itself
                event.accepted = true
            }
        }

        Repeater {
            id: boxRepeater
            model: Controller.boxModel
            delegate: Rectangle {
                id: boxDelegate
                required property int index
                required property real boxX
                required property real boxY
                required property real boxWidth
                required property real boxHeight
                required property string boxText
                required property int boxCheckStatus
                required property int boxKind

                property bool isImage: Controller.boxModel.isImageBox(boxDelegate.index)
                property bool isSelected: Controller.selectedBoxIndex === boxDelegate.index

                // One hue per block kind; selection overrides the frame.
                readonly property color kindOuter: boxKind == 1 ? Theme.overlayImageOuter
                                                                 : boxKind == 2 ? Theme.overlayTableOuter
                                                                 : boxKind == 3 ? Theme.overlayFormulaOuter
                                                                 : Theme.overlayTextOuter
                readonly property color kindInner: boxKind == 1 ? Theme.overlayImageInner
                                                                  : boxKind == 2 ? Theme.overlayTableInner
                                                                  : boxKind == 3 ? Theme.overlayFormulaInner
                                                                  : Theme.overlayTextInner
                readonly property color kindFill: boxKind == 1 ? Theme.overlayImageFill
                                                                 : boxKind == 2 ? Theme.overlayTableFill
                                                                 : boxKind == 3 ? Theme.overlayFormulaFill
                                                                 : Theme.overlayTextFill

                color: kindFill
                // Keep the selected frame and its mini toolbar on top of the
                // neighboring blocks.
                z: isSelected ? 2 : 0
                border.color: isSelected ? Theme.accent : kindOuter
                border.width: isSelected ? 2 : 1
                x: boxX * imageArea.width
                y: boxY * imageArea.height
                width: boxWidth * imageArea.width
                height: boxHeight * imageArea.height

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    color: "transparent"
                    border.color: isSelected ? Theme.accent : kindInner
                    border.width: 1
                }

                Rectangle {
                    id: statusDot
                    // Outside the block, to the left: gray = recognized (by
                    // page OCR or block recognition, no verdict yet), green =
                    // verified Ok, amber = re-recognized (two text variants),
                    // red = Mismatch/Review. A markup block without text shows
                    // no dot.
                    visible: boxCheckStatus !== 0 || boxText.length > 0
                    x: boxDelegate.x < 14 ? 2 : -12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: boxCheckStatus === 1 ? Theme.success
                         : boxCheckStatus === 2 ? Theme.warning
                         : boxCheckStatus >= 3 ? Theme.error
                         : Theme.nothing
                }

                MouseArea {
                    anchors.fill: parent
                    visible: !boxDelegate.isImage
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    onClicked: {
                        Controller.selectedBoxIndex = boxDelegate.index
                        imageArea.forceActiveFocus()
                    }
                }

                // --- Image blocks: drag & boundary editing (existing behavior) ---
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
                        Controller.selectedBoxIndex = boxDelegate.index
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
                        Controller.onBoxRectChanged(boxDelegate.index, nx, ny,
                                                    boxWidth, boxHeight)
                    }
                }//MouseArea
            }//delegate: Rectangle
        }//Repeater

        // One floating toolbar and one handle set for the selected block
        // (QML-8): per-delegate instances cost ~30 live objects per box and
        // were recreated on every page change. z above the selected box's z:2.
        readonly property var selectedDelegate: Controller.selectedBoxIndex >= 0
                                                ? boxRepeater.itemAt(Controller.selectedBoxIndex)
                                                : null

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

        BlockMiniToolbar {
            z: 3
            visible: imageArea.selectedDelegate !== null
            isTextBlock: imageArea.selectedDelegate
                         ? !imageArea.selectedDelegate.isImage : true

            x: {
                const sel = imageArea.selectedDelegate
                if (!sel)
                    return 0
                const centered = sel.x + (sel.width - width) / 2
                return Math.max(0, Math.min(centered, imageArea.width - width))
            }
            y: {
                const sel = imageArea.selectedDelegate
                if (!sel)
                    return 0
                return sel.y >= height + Theme.spacingLarge
                       ? sel.y - height - Theme.spacingSmall
                       : sel.y + sel.height + Theme.spacingSmall
            }
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

                z: 3
                width: 10
                height: 10
                radius: 2
                color: Theme.surfaceAlt
                border.color: Theme.overlayImageOuter
                border.width: 1
                visible: imageArea.selectedDelegate !== null
                         && imageArea.selectedDelegate.isImage

                x: {
                    const sel = imageArea.selectedDelegate
                    if (!sel)
                        return 0
                    const m = modelData.mode
                    if (m === "NW" || m === "W" || m === "SW")
                        return sel.x - 5
                    if (m === "N" || m === "S")
                        return sel.x + (sel.width - width) / 2
                    return sel.x + sel.width - 5
                }
                y: {
                    const sel = imageArea.selectedDelegate
                    if (!sel)
                        return 0
                    const m = modelData.mode
                    if (m === "NW" || m === "N" || m === "NE")
                        return sel.y - 5
                    if (m === "W" || m === "E")
                        return sel.y + (sel.height - height) / 2
                    return sel.y + sel.height - 5
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: handle.modelData.cursor

                    property real startX: 0
                    property real startY: 0
                    property real startW: 0
                    property real startH: 0
                    property real grabX: 0
                    property real grabY: 0

                    onPressed: (mouse) => {
                        const sel = imageArea.selectedDelegate
                        startX = sel.boxX
                        startY = sel.boxY
                        startW = sel.boxWidth
                        startH = sel.boxHeight
                        const p = mapToItem(imageArea, mouse.x, mouse.y)
                        grabX = p.x
                        grabY = p.y
                    }
                    onPositionChanged: (mouse) => {
                        const sel = imageArea.selectedDelegate
                        const p = mapToItem(imageArea, mouse.x, mouse.y)
                        const r = imageArea.resizeRect(handle.modelData.mode,
                            startX, startY, startW, startH,
                            (p.x - grabX) / imageArea.width,
                            (p.y - grabY) / imageArea.height)
                        Controller.onBoxRectChanged(sel.index, r.x, r.y, r.w, r.h)
                    }
                }//MouseArea
            }//delegate: Rectangle
        }//Repeater
    }//Item
}