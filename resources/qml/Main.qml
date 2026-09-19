pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import LLocr

import "Common"
import "MainWindow"
import "Settings"

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

    header: Header {
        onOpenFileRequested: fileDialog.open()
        onExportRequested: (multiPage) => {
            if (multiPage) {
                exportOptionsDialog.open()
            } else {
                exportDialog.scope = 0
                exportDialog.open()
            }
        }
        onOpenUiSettingsRequested: uiSettingsWindow.show()
        onOpenOutputSettingsRequested: outputSettingsWindow.show()
        onOpenRuntimeSettingsRequested: runtimeSettingsWindow.show()
        onOpenOcrModelSettingsRequested: ocrModelSettingsWindow.show()
        onOpenCheckModelSettingsRequested: checkModelSettingsWindow.show()
    }

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
            visible: Controller.hasImage
        }

        ImagePanel {
            SplitView.preferredWidth: parent.width * 0.45
            SplitView.minimumWidth: 300
        }

        WorkPanel {
            SplitView.minimumWidth: 300
        }


    } //SplitView

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
            Controller.openFiles(urls)
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

        LLOLabel {
            anchors.centerIn: parent
            text: qsTr("Drop to open")
            font.pointSize: Theme.titleSize
            color: Theme.accent
        }
    }//Rectangle

    FileDialog {
        id: fileDialog
        title: qsTr("Open images, PDF or DjVu")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("Documents (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.pdf *.djvu *.djv)"),
            qsTr("All files (*)")
        ]
        onAccepted: Controller.openFiles(selectedFiles)
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export recognized text")
        fileMode: FileDialog.SaveFile
        nameFilters: Controller.exportNameFilters

        property int scope: 0 //0 = all, 1 = current, 2 = range
        property int fromPage: 1
        property int toPage: 1

        onAccepted: Controller.exportPages(selectedFile,
                                           exportDialog.scope,
                                           exportDialog.fromPage,
                                           exportDialog.toPage)
    }

    footer: Footer {
        logWindow: serverLogWindow
    }

    ServerLogWindow {
        id: serverLogWindow
    }

    UiSettingsWindow {
        id: uiSettingsWindow
    }

    OutputSettingsWindow {
        id: outputSettingsWindow
    }

    RuntimeSettingsWindow {
        id: runtimeSettingsWindow
        setupWizardRef: setupWizard
        logWindowRef: serverLogWindow
    }

    ModelSettingsWindow {
        id: ocrModelSettingsWindow
        role: "ocr"
    }

    ModelSettingsWindow {
        id: checkModelSettingsWindow
        role: "check"
    }

    ExportDialog {
        id: exportOptionsDialog
        onExportRequested: (scope, fromPage, toPage) => {
            exportDialog.scope = scope
            exportDialog.fromPage = fromPage
            exportDialog.toPage = toPage
            exportDialog.open()
        }
    }

    SetupWizard {
        id: setupWizard

        onOpenConnectionSettings: runtimeSettingsWindow.show()
    }

    Timer {
        id: setupTrigger
        interval: 400
        repeat: false
        onTriggered: {
            if (Settings.setupVersion === 0 && !Settings.setupDismissed)
                setupWizard.startWizard()
        }
    }
    Component.onCompleted: {
        setupTrigger.start()
    }
}
