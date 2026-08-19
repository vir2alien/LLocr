import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import LLocr

ApplicationWindow {
    id: mainWindow
    width: 1360
    height: 820
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("LLM OCR")

    color: Theme.background

    property int imageRevision: 0
    property int docRevision: 0

    Connections {
        target: controller
        function onPageChanged() { mainWindow.imageRevision++ }
        function onDocumentChanged() {
            mainWindow.docRevision++
            mainWindow.imageRevision++
        }
    }

    ButtonGroup {
        id: themeGroup
    }

    WindowSettings {
        window: mainWindow
    }

    header: ToolBar {
        leftPadding: Theme.spacing
        rightPadding: Theme.spacing

        background: Rectangle {
            color: Theme.surface

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.divider
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            ToolButton {
                text: qsTr("Open…")
                onClicked: fileDialog.open()
            }

            ToolSeparator {}

            ToolButton {
                text: qsTr("Recognize")
                enabled: controller.hasImage && !controller.busy
                         && controller.canRecognize
                onClicked: controller.recognizeCurrent()
            }
            ToolButton {
                text: qsTr("Recognize all")
                enabled: controller.hasImage && !controller.busy
                         && controller.pageCount > 1
                         && controller.canRecognize
                onClicked: controller.recognizeAll()
            }
            ToolButton {
                text: qsTr("Stop")
                enabled: controller.busy
                onClicked: controller.stop()
            }

            BusyIndicator {
                running: controller.busy
                visible: controller.busy
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }

            ToolSeparator { visible: controller.pageCount > 1 }

            RowLayout {
                visible: controller.pageCount > 1
                spacing: 0

                ToolButton {
                    text: "\u2039"
                    enabled: controller.currentPage > 0
                    onClicked: controller.currentPage = controller.currentPage - 1
                }
                Label {
                    text: (controller.currentPage + 1) + " / " + controller.pageCount
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    Layout.minimumWidth: 56
                }
                ToolButton {
                    text: "\u203a"
                    enabled: controller.currentPage < controller.pageCount - 1
                    onClicked: controller.currentPage = controller.currentPage + 1
                }
            }

            ToolSeparator {}

            ToolButton {
                text: qsTr("Export…")
                enabled: controller.hasResult
                onClicked: {
                    if (controller.pageCount > 1) {
                        exportOptionsDialog.open()
                    } else {
                        exportDialog.scope = 0 //export all
                        exportDialog.open()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                text: controller.statusMessage
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                Layout.maximumWidth: 380
            }

            ToolButton {
                text: qsTr("Settings...")
                onClicked: settingsDialog.open()
            }
        }
    } // ToolBar

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        handle: Rectangle {
            implicitWidth: 5
            color: SplitHandle.pressed || SplitHandle.hovered
                   ? Theme.border : Theme.background

            Rectangle {  // hairline, always visible
                anchors.centerIn: parent
                width: 1
                height: parent.height
                color: Theme.divider
            }
        }

        Rectangle {// thumbnails panel
            id: thumbPanel
            SplitView.preferredWidth: 170
            SplitView.minimumWidth: 120
            SplitView.maximumWidth: 300
            color: Theme.surface
            visible: controller.hasImage

            ListView {
                id: thumbList
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spacing
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                cacheBuffer: 10000

                // Пока идёт перетаскивание — блокируем прокрутку колёсиком/жестами,
                // чтобы индексы не «плыли».
                interactive: draggedIndex === -1

                model: controller.pageModel

                // Индекс перетаскиваемого делегата (-1 если нет)
                property int draggedIndex: -1

                displaced: Transition {
                    NumberAnimation { property: "y"; duration: 150; easing.type: Easing.OutQuad }
                }

                delegate: Item {
                    id: delegateRoot
                    width: thumbList.width - Theme.spacingSmall
                    height: width * 1.3 + 22

                    property int pageIdx: model.pageIndex
                    property bool dragActive: dragHandler.active

                    // Во время перетаскивания приподнимаем над остальными
                    z: dragActive ? 10 : 1

                    HoverHandler { id: thumbHover }

                    Rectangle {
                        id: card
                        anchors.fill: parent
                        radius: Theme.radius
                        color: model.current ? Theme.selected : "transparent"
                        border.color: dragActive ? Theme.accent
                                      : (model.current ? Theme.accent : Theme.divider)
                        border.width: 1

                        scale: delegateRoot.dragActive ? 1.03 : 1.0
                        Behavior on scale { NumberAnimation { duration: 100 } }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 3

                            Image {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: true
                                source: "image://ocr/page/" + model.pageIndex
                                        + "?r=" + mainWindow.docRevision
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                Rectangle {
                                    width: 9
                                    height: 9
                                    radius: model.edited ? 2 : 5
                                    color: model.edited ? Theme.textPrimary
                                           : (model.recognized ? Theme.textSecondary
                                                               : "transparent")
                                    border.width: model.recognized || model.edited ? 0 : 1
                                    border.color: Theme.textMuted

                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: model.edited ? qsTr("Edited")
                                                     : (model.recognized ? qsTr("Recognized")
                                                                         : qsTr("Not recognized"))
                                }
                                Label {
                                    text: qsTr("Page %1").arg(model.pageIndex + 1)
                                    font.pixelSize: Theme.fontSmall
                                    color: model.recognized ? Theme.textSecondary
                                                            : Theme.textMuted
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        TapHandler {
                            onTapped: controller.currentPage = model.pageIndex
                        }
                    }// Rectangle card

                    ToolButton {
                        id: deletePageButton
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 2
                        implicitWidth: 20
                        implicitHeight: 20
                        padding: 0
                        visible: thumbHover.hovered && !controller.busy && thumbList.draggedIndex === -1
                        enabled: !controller.busy
                        opacity: visible ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 100 } }
                        text: "\u2715"

                        background: Rectangle {
                            radius: width / 2
                            color: deletePageButton.hovered ? Theme.border : Theme.surfaceAlt
                            border.color: Theme.divider
                            border.width: 1
                        }

                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Delete page")
                        Accessible.name: qsTr("Delete page %1").arg(model.pageIndex + 1)

                        onClicked: controller.removePage(model.pageIndex)
                    }// ToolButton deletePageButton

                    Rectangle {
                        id: dragGrip
                        width: 24
                        height: 24
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 2
                        radius: 3
                        z: 2
                        visible: (thumbHover.hovered || dragActive) && !controller.busy
                        opacity: visible ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 100 } }
                        color: dragActive ? Theme.accent : Theme.surfaceAlt
                        border.color: Theme.divider
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "\u22EE\u22EE"
                            color: Theme.textSecondary
                            font.pixelSize: 9
                        }

                        DragHandler {
                            id: dragHandler
                            target: delegateRoot
                            enabled: !controller.busy
                            cursorShape: Qt.SizeVerCursor

                            // Захватываем только вертикальное перемещение
                            yAxis.enabled: true
                            xAxis.enabled: false

                            property int fromIndex: -1

                            onActiveChanged: {
                                if (active) {
                                    fromIndex = index
                                    thumbList.draggedIndex = index
                                } else {
                                    // Вычисляем целевой индекс по центру делегата
                                    const centerY = delegateRoot.y + delegateRoot.height / 2
                                    let toIndex = Math.floor(centerY / (delegateRoot.height + thumbList.spacing))
                                    toIndex = Math.max(0, Math.min(thumbList.count - 1, toIndex))

                                    thumbList.draggedIndex = -1

                                    if (fromIndex !== -1 && fromIndex !== toIndex)
                                        controller.movePage(fromIndex, toIndex)
                                    else
                                        delegateRoot.y = index * (delegateRoot.height + thumbList.spacing) // вернуть на место
                                }
                            }
                        }
                    }//Rectangle dragGrip
                }//delegate
            }//ListView
        } // Rectangle thumbPanel

        Rectangle { // image preview
            id: leftPanel
            SplitView.preferredWidth: parent.width * 0.45
            SplitView.minimumWidth: 300
            color: Theme.surfaceSunken

            Image {
                id: previewImage
                anchors.fill: parent
                anchors.margins: Theme.spacingLarge
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

                    Repeater {
                        model: controller.boxModel
                        delegate: Rectangle {
                            color: "transparent"
                            border.color: Theme.overlayOuter
                            border.width: 1
                            x: boxX * imageArea.width
                            y: boxY * imageArea.height
                            width: boxWidth * imageArea.width
                            height: boxHeight * imageArea.height

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 1
                                color: "transparent"
                                border.color: Theme.overlayInner
                                border.width: 1
                            }

                            Rectangle {
                                visible: boxLabel.length > 0
                                color: Theme.overlayOuter
                                radius: 2
                                height: labelText.height + 2
                                width: labelText.width + 6
                                Text {
                                    id: labelText
                                    anchors.centerIn: parent
                                    text: boxLabel
                                    font.pixelSize: 10
                                    color: Theme.overlayInner
                                }
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !controller.hasImage
                text: qsTr("Open an image or PDF to begin")
                color: Theme.textMuted
            }
        } // Rectangle img preview

        Rectangle {//recognized text
            SplitView.minimumWidth: 300
            color: Theme.surfaceAlt

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spacingSmall

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    visible: controller.currentPageEditable

                    Label {
                        text: controller.currentPageEdited ? qsTr("Edited")
                                                           : qsTr("Recognized")
                        color: controller.currentPageEdited ? Theme.textPrimary
                                                           : Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        font.bold: controller.currentPageEdited
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        flat: true
                        text: qsTr("Revert")
                        visible: controller.currentPageEdited
                        onClicked: controller.revertCurrentPageEdits()
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TextArea {
                        id: textArea
                        readOnly: !controller.currentPageEditable
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        placeholderText: qsTr("Recognized text will appear here")
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted

                        background: null

                        property bool syncing: false

                        function reload() {
                            var t = controller.resultText
                            if (text === t)
                                return
                            syncing = true
                            text = t
                            syncing = false
                        }

                        onTextChanged: {
                            if (!syncing)
                                controller.setCurrentPageText(text)
                        }

                        Component.onCompleted: reload()

                        Connections {
                            target: controller
                            function onResultChanged() { textArea.reload() }
                        }
                    }
                }
            }
        } //Rectangle recognized text
    } // SplitView

    DropArea {
        id: dropArea
        anchors.fill: parent

        onEntered: (drag) => {
            if (drag.hasUrls)
                drag.accept()
        }
        onDropped: (drop) => {
            const urls = drop.urls.map(function(u) { return u })
            drop.accept()
            controller.openFiles(urls)
        }
    }

    Rectangle {
        id: dropFeedback
        anchors.fill: parent
        z: 100
        visible: dropArea.containsDrag
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
        radius: Theme.radius

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Theme.radius - 1
            color: Theme.accent
            opacity: 0.08
        }

        Label {
            anchors.centerIn: parent
            text: qsTr("Drop to open")
            font.pixelSize: Theme.fontCaption * 2
            color: Theme.accent
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open images or PDF")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("Documents (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.pdf)"),
            qsTr("All files (*)")
        ]
        onAccepted: controller.openFiles(selectedFiles)
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export recognized text")
        fileMode: FileDialog.SaveFile
        nameFilters: controller.exportNameFilters

        property int scope: 0 //0 = all, 1 = current, 2 = range
        property int fromPage: 1
        property int toPage: 1

        onAccepted: controller.exportPages(selectedFile,
                                           exportDialog.scope,
                                           exportDialog.fromPage,
                                           exportDialog.toPage)
    }

    SettingsDialog {
        id: settingsDialog
    }

    ExportDialog {
        id: exportOptionsDialog
    }
}
