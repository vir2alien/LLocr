import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    width: 1360
    height: 820
    visible: true
    title: qsTr("LLM OCR")

    property int imageRevision: 0
    property int docRevision: 0

    Connections {
        target: controller
        function onPageChanged() { window.imageRevision++ }
        function onDocumentChanged() {
            window.docRevision++
            window.imageRevision++
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            Button {
                text: qsTr("Open…")
                onClicked: fileDialog.open()
            }

            Button {
                text: qsTr("Recognize")
                enabled: controller.hasImage && !controller.busy
                         && controller.canRecognize
                onClicked: controller.recognizeCurrent()
            }
            Button {
                text: qsTr("Recognize all")
                enabled: controller.hasImage && !controller.busy
                         && controller.pageCount > 1
                         && controller.canRecognize
                onClicked: controller.recognizeAll()
            }

            RowLayout {
                visible: controller.pageCount > 1
                spacing: 4

                Button {
                    text: "‹"
                    enabled: controller.currentPage > 0
                    onClicked: controller.currentPage = controller.currentPage - 1
                }
                Label {
                    text: (controller.currentPage + 1) + " / " + controller.pageCount
                    color: "#dddddd"
                }
                Button {
                    text: "›"
                    enabled: controller.currentPage < controller.pageCount - 1
                    onClicked: controller.currentPage = controller.currentPage + 1
                }
            }

            Button {
                text: qsTr("Stop")
                enabled: controller.busy
                onClicked: controller.stop()
            }

            Button {
                text: qsTr("Export…")
                enabled: controller.hasResult
                onClicked: {
                    if (controller.pageCount > 1) {
                        exportOptionsDialog.open()
                    } else {
                        exportDialog.scope = 0 // ExportAll
                        exportDialog.open()
                    }
                }
            }

            BusyIndicator {
                running: controller.busy
                visible: controller.busy
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }

            Item { Layout.fillWidth: true }  // spacer

            Button {
                text: qsTr("Settings")
                onClicked: settingsDialog.open()
            }

            Label {
                text: controller.statusMessage
                elide: Text.ElideRight
                Layout.maximumWidth: 380
            }
        }
    } // ToolBar

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Rectangle {// thumbnails panel
            id: thumbPanel
            SplitView.preferredWidth: 170
            SplitView.minimumWidth: 120
            SplitView.maximumWidth: 300
            color: "#232323"
            visible: controller.hasImage

            ListView {
                id: thumbList
                anchors.fill: parent
                anchors.margins: 6
                spacing: 8
                clip: true
                model: controller.pageModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    width: thumbList.width - 6
                    height: width * 1.3 + 22

                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: model.current ? "#3a3f4b" : "transparent"
                        border.color: model.current ? "#4fc3f7" : "#3a3a3a"
                        border.width: model.current ? 2 : 1

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
                                        + "?r=" + window.docRevision
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                // Status marker:
                                //   amber = edited, green = recognized, grey = pending.
                                Rectangle {
                                    width: 10; height: 10; radius: 5
                                    color: model.edited ? "#ffb74d"
                                           : (model.recognized ? "#66bb6a" : "#666666")
                                }
                                Label {
                                    text: qsTr("Page ") + (model.pageIndex + 1)
                                    font.pixelSize: 11
                                    color: model.recognized ? "#cccccc" : "#888888"
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            // Navigation allowed even while recognizing.
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
            color: "#2b2b2b"

            Image {
                id: previewImage
                anchors.fill: parent
                anchors.margins: 12
                fillMode: Image.PreserveAspectFit
                source: controller.hasImage
                        ? "image://ocr/current?" + window.imageRevision
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
                            border.color: "#4fc3f7"
                            border.width: 2
                            x: boxX * imageArea.width
                            y: boxY * imageArea.height
                            width: boxWidth * imageArea.width
                            height: boxHeight * imageArea.height

                            Rectangle {
                                visible: boxLabel.length > 0
                                color: "#4fc3f7"
                                height: labelText.height + 2
                                width: labelText.width + 6
                                Text {
                                    id: labelText
                                    anchors.centerIn: parent
                                    text: boxLabel
                                    font.pixelSize: 10
                                    color: "#000000"
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
                color: "#888888"
            }
        } // Rectangle img preview

        Rectangle {//recognized text
            SplitView.minimumWidth: 300
            color: "#1e1e1e"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: controller.currentPageEditable

                    Label {
                        text: controller.currentPageEdited ? qsTr("Edited")
                                                           : qsTr("Recognized")
                        color: controller.currentPageEdited ? "#ffb74d" : "#66bb6a"
                        font.pixelSize: 12
                    }
                    Item { Layout.fillWidth: true }
                    Button {
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

                        property bool syncing: false

                        function reload() {
                            var t = controller.resultText
                            if (text === t)
                                return   // unchanged -> keep the caret intact
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
                            // Fires on navigation, fresh recognition, revert.
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

        property int scope: 0 //0 = All, 1 = Current, 2 = Range (1-based).
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
                font.pixelSize: 11
                color: "#999999"
                text: qsTr("Only recognized pages inside the selection are exported.")
            }
        }
    }

    SettingsDialog {
        id: settingsDialog
    }
}
