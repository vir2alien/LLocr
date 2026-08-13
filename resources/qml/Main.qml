import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
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

    Material.theme: uiController.dark ? Material.Dark : Material.Light
    Material.accent: Theme.accent
    Material.foreground: Theme.textPrimary

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
                model: controller.pageModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    width: thumbList.width - Theme.spacingSmall
                    height: width * 1.3 + 22

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: model.current ? Theme.selected : "transparent"
                        border.color: model.current ? Theme.accent : Theme.divider
                        border.width: 1

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

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: controller.currentPage = model.pageIndex
                        }
                    }
                }
            }
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

    FileDialog {
        id: fileDialog
        title: qsTr("Open image or PDF")
        nameFilters: [
            qsTr("Documents (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.pdf)"),
            qsTr("All files (*)")
        ]
        onAccepted: controller.openDocument(selectedFile)
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

    Dialog {
        id: exportOptionsDialog
        title: qsTr("Export — pages")
        modal: true
        anchors.centerIn: parent
        width: 360
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAboutToShow: {
            scopeAll.checked = true
            fromSpin.value = 1
            toSpin.value = controller.pageCount
        }

        onAccepted: {
            exportDialog.scope = scopeCurrent.checked ? 1
                                : (scopeRange.checked ? 2 : 0)
            exportDialog.fromPage = fromSpin.value
            exportDialog.toPage = toSpin.value
            exportDialog.open()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            ButtonGroup { id: scopeGroup }

            RadioButton {
                id: scopeAll
                text: qsTr("All recognized pages")
                checked: true
                ButtonGroup.group: scopeGroup
            }
            RadioButton {
                id: scopeCurrent
                text: qsTr("Current page (%1)").arg(controller.currentPage + 1)
                ButtonGroup.group: scopeGroup
            }
            RadioButton {
                id: scopeRange
                text: qsTr("Page range")
                ButtonGroup.group: scopeGroup
            }

            RowLayout {
                Layout.leftMargin: 28
                spacing: 6
                enabled: scopeRange.checked

                Label { text: qsTr("from") }
                SpinBox {
                    id: fromSpin
                    from: 1
                    to: controller.pageCount
                    editable: true
                }
                Label { text: qsTr("to") }
                SpinBox {
                    id: toSpin
                    from: 1
                    to: controller.pageCount
                    editable: true
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
                text: qsTr("Only recognized pages inside the selection are exported.")
            }
        }
    }

    SettingsDialog {
        id: settingsDialog
    }
}
