import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import LLocr

import "MainWindow"

ApplicationWindow {
    id: mainWindow
    width: 1360
    height: 820
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("LLM OCR")

    color: Theme.background
    ButtonGroup {
        id: themeGroup
    }

    WindowSettings {
        window: mainWindow
    }

    header: Header {}

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        handle: Rectangle {
            implicitWidth: 5
            color: SplitHandle.pressed || SplitHandle.hovered
                   ? Theme.border : Theme.background

            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: parent.height
                color: Theme.divider
            }
        }

        ThumbPanel {
            SplitView.preferredWidth: 170
            SplitView.minimumWidth: 120
            SplitView.maximumWidth: 300
            color: Theme.surface
            visible: controller.hasImage
        }

        ImagePanel {
            SplitView.preferredWidth: parent.width * 0.45
            SplitView.minimumWidth: 300
        }

        Rectangle { // recognized text + preview
            SplitView.minimumWidth: 300
            color: Theme.surfaceAlt
            Label {
                anchors.centerIn: parent
                verticalAlignment: Text.AlignVCenter
                visible: !controller.hasResult && !previewSwitch.checked
                text: qsTr("Recognized text will appear here")
                color: Theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                width: Math.min(implicitWidth, parent.width - 2 * Theme.spacing)
            }

            ColumnLayout {
                anchors.fill: parent
                visible: controller.hasResult
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: previewSwitch.implicitHeight + 2*Theme.spacingSmall
                    spacing: Theme.spacing
                    Layout.margins: Theme.spacingSmall

                    Label {
                        visible: controller.currentPageEditable
                        text: controller.currentPageEdited ? qsTr("Edited")
                                                           : qsTr("Recognized")
                        color: controller.currentPageEdited ? Theme.textPrimary
                                                           : Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        font.bold: controller.currentPageEdited
                    }

                    Button {
                        flat: true
                        text: qsTr("Revert")
                        visible: controller.currentPageEditable && controller.currentPageEdited
                        onClicked: controller.revertCurrentPageEdits()
                    }

                    Item { Layout.fillWidth: true }
                    Switch {
                        id: previewSwitch
                        Layout.alignment: Qt.AlignVCenter
                        topPadding: 0
                        bottomPadding: 0
                        text: qsTr("Preview")
                    }
                }// RowLayout text menu
                Rectangle {
                    Layout.fillWidth: true;
                    Layout.preferredHeight: 1
                    color: Theme.divider
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: previewSwitch.checked ? 1 : 0

                    // --- 0: Edit ---
                    ScrollView {
                        TextArea {
                            id: textArea
                            readOnly: !controller.currentPageEditable
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
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

                    // --- 1: Preview ---
                    Loader {
                        active: previewSwitch.checked
                        sourceComponent: previewComponent
                    }
                }
            }
        } // Rectangle recognized text + preview
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

    Component {
        id: previewComponent
        MarkdownPreview {
            markdown: controller.resolveImagesForPreview(textArea.text)
            dark: Theme.dark
            bgColor: Theme.surfaceAlt
            fgColor: Theme.textPrimary
        }
    }

    footer: ToolBar {
        leftPadding: Theme.spacing * 2
        rightPadding: Theme.spacing * 2
        height: Theme.controlHeight

        background: Rectangle {
            color: Theme.surface

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.divider
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Label {
                text: controller.statusMessage
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            BusyIndicator {
                running: controller.busy
                visible: controller.busy
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
        }
    }
}
