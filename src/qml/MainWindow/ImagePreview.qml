pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    // The zoom is relative to the fit-to-window size: 1 = the whole page in
    // view, 2 = twice as large.
    property real zoom: 1
    readonly property real minZoom: 0.25
    readonly property real maxZoom: 8
    // The page offset in screen pixels, relative to the centered position.
    property real panX: 0
    property real panY: 0
    // The block panel overlaps the preview's bottom edge; the page is kept
    // inside the free area above it.
    property real bottomInset: 0

    readonly property real usableHeight: Math.max(1, viewport.height - root.bottomInset)

    // The page may not be dragged out of the view; vertically it may scroll
    // up far enough for the block panel to uncover its bottom.
    function clampPan() {
        const w = viewport.width
        const h = viewport.height
        const uH = root.usableHeight
        const pw = pageImage.paintedWidth * root.zoom
        const ph = pageImage.paintedHeight * root.zoom
        const baseLeft = (w - pw) / 2
        const baseTop = (h - ph) / 2
        const minX = Math.min(0, w - pw) - baseLeft
        const maxX = Math.max(0, w - pw) - baseLeft
        const minY = Math.min(0, uH - ph) - baseTop
        const maxY = Math.max(0, h - ph) - baseTop
        root.panX = Math.max(minX, Math.min(maxX, root.panX))
        root.panY = Math.max(minY, Math.min(maxY, root.panY))
    }

    function zoomBy(factor) {
        root.zoomAt(viewport.width / 2, viewport.height / 2, factor)
    }

    // The page point under the cursor stays under the cursor.
    function zoomAt(cx, cy, factor) {
        const z2 = Math.max(root.minZoom, Math.min(root.maxZoom, root.zoom * factor))
        if (z2 === root.zoom)
            return
        if (pageImage.paintedWidth <= 0 || pageImage.paintedHeight <= 0) {
            root.zoom = z2
            return
        }
        const w = viewport.width
        const h = viewport.height
        const pw = pageImage.paintedWidth
        const ph = pageImage.paintedHeight
        const k = z2 / root.zoom
        const lx = cx - (root.panX + (w - pw * root.zoom) / 2)
        const ly = cy - (root.panY + (h - ph * root.zoom) / 2)
        root.zoom = z2
        root.panX = cx - lx * k - (w - pw * z2) / 2
        root.panY = cy - ly * k - (h - ph * z2) / 2
        root.clampPan()
    }

    function resetZoom() {
        root.zoom = 1
        root.panX = 0
        root.panY = 0
        root.clampPan()
    }

    // The selected block must stay clear of the block panel.
    function ensureVisible() {
        const sel = imageArea.selectedDelegate
        if (!sel)
            return
        const top = imageArea.y + sel.y
        const bottom = top + sel.height
        const left = imageArea.x + sel.x
        const right = left + sel.width
        const h = root.usableHeight
        if (bottom > h)
            root.panY -= bottom - h + Theme.spacingSmall
        else if (top < 0)
            root.panY -= top
        if (right > viewport.width)
            root.panX -= right - viewport.width + Theme.spacingSmall
        else if (left < 0)
            root.panX -= left
        root.clampPan()
    }

    function geometryChanged() {
        root.clampPan()
        root.ensureVisible()
    }

    onBottomInsetChanged: geometryChanged()

    Item {
        id: viewport
        anchors.fill: parent
        clip: true

        Image {
            id: pageImage
            width: viewport.width
            height: viewport.height
            fillMode: Image.PreserveAspectFit
            source: Controller.hasImage
                    ? "image://ocr/current?" + Controller.imageRevision
                    : ""
            cache: false

            transformOrigin: Item.TopLeft
            scale: root.zoom
            // The painted image is centered in the item (the default
            // alignment), so the item's offset puts the page where the pan
            // says it should be.
            readonly property real offsetX: (width - paintedWidth) / 2
            readonly property real offsetY: (height - paintedHeight) / 2
            x: root.panX + (viewport.width - pageImage.paintedWidth * root.zoom) / 2
               - pageImage.offsetX * root.zoom
            y: root.panY + (viewport.height - pageImage.paintedHeight * root.zoom) / 2
               - pageImage.offsetY * root.zoom

            onPaintedWidthChanged: root.geometryChanged()
            onPaintedHeightChanged: root.geometryChanged()
        }

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
            visible: pageImage.status === Image.Error
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

        // The overlay lives in screen pixels, so the boxes, the mini toolbar
        // and the resize handles keep their size at any zoom.
        Item {
            id: imageArea
            x: pageImage.x + pageImage.offsetX * root.zoom
            y: pageImage.y + pageImage.offsetY * root.zoom
            width: pageImage.paintedWidth * root.zoom
            height: pageImage.paintedHeight * root.zoom

            MouseArea {
                anchors.fill: parent
                property real lastX: 0
                property real lastY: 0
                property bool dragged: false

                onPressed: (mouse) => {
                    // The page moves with the drag, so the cursor position is
                    // tracked in the viewport's frame — the area's own frame
                    // moves with the page and would halve every step.
                    const p = mapToItem(viewport, mouse.x, mouse.y)
                    lastX = p.x
                    lastY = p.y
                    dragged = false
                    imageArea.forceActiveFocus()
                }
                onPositionChanged: (mouse) => {
                    const p = mapToItem(viewport, mouse.x, mouse.y)
                    const dx = p.x - lastX
                    const dy = p.y - lastY
                    if (!dragged && Math.abs(dx) + Math.abs(dy) < 3)
                        return
                    dragged = true
                    lastX = p.x
                    lastY = p.y
                    root.panX += dx
                    root.panY += dy
                    root.clampPan()
                }
                onClicked: {
                    if (!dragged)
                        Controller.selectedBoxIndex = -1
                }
            }

            Keys.onDeletePressed: (event) => {
                const index = Controller.selectedBoxIndex
                if (index >= 0 && !Controller.checkBusy && Controller.boxModel.isImageBox(index)
                        && Controller.removeBlock(index)) {
                    // removeBlock() clears the selection itself
                    event.accepted = true
                }
            }

            Keys.onPressed: (event) => {
                const step = event.modifiers & Qt.ShiftModifier ? 200 : 40
                if (event.key === Qt.Key_Left) {
                    root.panX -= step
                } else if (event.key === Qt.Key_Right) {
                    root.panX += step
                } else if (event.key === Qt.Key_Up) {
                    root.panY -= step
                } else if (event.key === Qt.Key_Down) {
                    root.panY += step
                } else {
                    return
                }
                root.clampPan()
                event.accepted = true
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
                    required property string boxCorrectedText
                    required property int boxCorrectedCheckStatus
                    required property int boxKind
                    required property real boxConfidence
                    required property real boxCorrectedConfidence
                    required property bool boxUseCorrectedText

                    property bool isImage: Controller.boxModel.isImageBox(boxDelegate.index)
                    property bool isSelected: Controller.selectedBoxIndex === boxDelegate.index

                    // The status dot follows the layer the user reads (ADR 185):
                    // the correction while the panel's toggle selects it, else
                    // the OCR text. A verdict-only correction (a fix REVIEW)
                    // speaks for the block — there is no corrected text to read.
                    readonly property bool correctionActive: boxCorrectedText.length > 0 || boxCorrectedCheckStatus !== 0
                    readonly property int effectiveStatus: boxUseCorrectedText && correctionActive ? boxCorrectedCheckStatus : boxCheckStatus
                    readonly property real effectiveConfidence: boxUseCorrectedText && correctionActive ? boxCorrectedConfidence : boxConfidence
                    readonly property string effectiveText: boxUseCorrectedText && boxCorrectedText.length > 0 ? boxCorrectedText : boxText

                    // One hue per block kind; selection overrides the frame.
                    readonly property color kindOuter: boxKind == 1 ? Theme.overlayImageOuter
                                                                     : boxKind == 2 ? Theme.overlayTableOuter
                                                                     : boxKind == 3 ? Theme.overlayFormulaOuter
                                                                     : boxKind == 4 ? Theme.overlayServiceOuter
                                                                     : Theme.overlayTextOuter
                    readonly property color kindInner: boxKind == 1 ? Theme.overlayImageInner
                                                                      : boxKind == 2 ? Theme.overlayTableInner
                                                                      : boxKind == 3 ? Theme.overlayFormulaInner
                                                                      : boxKind == 4 ? Theme.overlayServiceInner
                                                                      : Theme.overlayTextInner
                    readonly property color kindFill: boxKind == 1 ? Theme.overlayImageFill
                                                                     : boxKind == 2 ? Theme.overlayTableFill
                                                                     : boxKind == 3 ? Theme.overlayFormulaFill
                                                                     : boxKind == 4 ? Theme.overlayServiceFill
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
                        // Outside the block, to the left: gray = recognized or
                        // corrected (no verdict on the layer in view), green =
                        // verified Ok or approved by hand, red = Mismatch or the
                        // damaged Review. With
                        // the confidence option on, a decision verdict (green/red)
                        // renders as its probability in percent inside a circle of
                        // the verdict's color. A markup block without text shows
                        // nothing.
                        readonly property bool confidenceBadge: Settings.showConfidence
                                                                && (effectiveStatus === 1 || effectiveStatus === 4)
                                                                && effectiveConfidence >= 0
                        visible: boxText.length > 0 || boxCorrectedText.length > 0
                        x: boxDelegate.x < 14 ? 2 : (confidenceBadge ? -24 : -12)
                        anchors.verticalCenter: parent.verticalCenter
                        width: confidenceBadge ? 20 : 8
                        height: confidenceBadge ? 20 : 8
                        radius: width / 2
                        color: effectiveStatus === 1 || effectiveStatus === 5 ? Theme.success
                             : effectiveStatus >= 3 ? Theme.error
                             : Theme.nothing

                        Text {
                            anchors.centerIn: parent
                            visible: parent.confidenceBadge
                            text: Math.round(effectiveConfidence * 100)
                            font.pointSize: Theme.captionSize
                            font.bold: true
                            color: "#ffffff"
                        }
                    }

                    // Drag & boundary editing: every block can be moved and
                    // resized, the manually added ones included — a block the
                    // user placed must be placeable.
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: boxDelegate.isImage ? Qt.SizeAllCursor : Qt.PointingHandCursor
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
                blockText: imageArea.selectedDelegate
                           ? imageArea.selectedDelegate.effectiveText : ""

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
                    { mode: "SW", cursor: Qt.SizeFDiagCursor },
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
        }//Item: imageArea

        WheelHandler {
            onWheel: (event) => {
                const delta = event.angleDelta
                if (event.modifiers & Qt.ControlModifier) {
                    const step = delta.y / 120
                    root.zoomAt(event.x, event.y, step > 0 ? 1.15 : 1 / 1.15)
                } else if (event.modifiers & Qt.ShiftModifier) {
                    // The horizontal delta only appears with Shift on some
                    // platforms.
                    const d = delta.x !== 0 ? delta.x : delta.y
                    root.panX += (d / 120) * 40
                    root.clampPan()
                } else {
                    // The page follows the gesture: the wheel down moves the
                    // page up and shows what is below.
                    root.panY += (delta.y / 120) * 40
                    root.clampPan()
                }
            }
        }

        // Photoshop-style zoom shortcuts: Ctrl/Cmd + +/- and Ctrl/Cmd + 0
        // (fit).
        Shortcut {
            sequences: ["Ctrl+Plus", "Ctrl+Equal"]
            onActivated: root.zoomBy(1.25)
        }
        Shortcut {
            sequences: ["Ctrl+Minus"]
            onActivated: root.zoomBy(1 / 1.25)
        }
        Shortcut {
            sequences: ["Ctrl+0"]
            onActivated: root.resetZoom()
        }
    }//Item: viewport

    Connections {
        target: Controller
        function onSelectedBoxChanged() {
            root.ensureVisible()
        }
        function onPageChanged() {
            root.ensureVisible()
        }
    }
}
