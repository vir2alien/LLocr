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
    title: Controller.projectFileName !== ""
           ? qsTr("LLM OCR — %1").arg(Controller.projectFileName)
           : qsTr("LLM OCR")

    color: Theme.background

    WindowSettings {
        window: mainWindow
    }

    // The secondary windows are top-level and non-modal: closing the main
    // window must take the process (and the managed llama-server) with it.
    onClosing: Qt.quit()

    function saveCurrentProject()
    {
        if (Controller.projectPath && Controller.projectPath.toString() !== "")
            Controller.saveProject(Controller.projectPath)
        else
            saveProjectDialog.open()
    }

    // The secondary windows are created on first use (QML-13): a Loader is the
    // window's handle, and windowProxy() keeps the consumers' bringToFront()
    // API (the footer and the runtimeSettingsRef tabs) without pre-creating
    // anything.
    function showWindow(loader) {
        if (!loader.active)
            loader.active = true
        loader.item.bringToFront()
    }

    function windowProxy(loader) {
        return { bringToFront: function() { showWindow(loader) } }
    }

    function openSetupWizard() {
        setupWizardLoader.active = true
        const wizard = setupWizardLoader.item as SetupWizard
        wizard.startWizard()
    }

    header: Header {
        onOpenFileRequested: fileDialog.open()
        onOpenProjectRequested: openProjectDialog.open()
        onSaveProjectRequested: mainWindow.saveCurrentProject()
        onSaveProjectAsRequested: saveProjectDialog.open()
        onExportRequested: (multiPage) => {
            if (multiPage) {
                exportOptionsDialog.open()
            } else {
                exportDialog.scope = 0
                exportDialog.open()
            }
        }
        onSetupWizardRequested: mainWindow.openSetupWizard()
        onOpenUiSettingsRequested: showWindow(uiSettingsLoader)
        onOpenOutputSettingsRequested: showWindow(outputSettingsLoader)
        onOpenVerificationSettingsRequested: showWindow(verificationSettingsLoader)
        onOpenRuntimeSettingsRequested: showWindow(runtimeSettingsLoader)
        onOpenTaskModelsSettingsRequested: showWindow(taskModelsSettingsLoader)
        onOpenOcrModelSettingsRequested: showWindow(ocrModelSettingsLoader)
        onOpenDecisionModelSettingsRequested: showWindow(decisionModelSettingsLoader)
        onOpenCheckModelSettingsRequested: showWindow(checkModelSettingsLoader)
        onOpenLayoutModelSettingsRequested: showWindow(layoutModelSettingsLoader)
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
            onSavePageRequested: {
                exportOptionsDialog.presetScope = 1
                exportOptionsDialog.open()
            }
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

    FileDialog {
        id: openProjectDialog
        title: qsTr("Open project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("LLocr project (*.llocr)")]
        onAccepted: Controller.openProject(selectedFile)
    }

    FileDialog {
        id: saveProjectDialog
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "llocr"
        nameFilters: [qsTr("LLocr project (*.llocr)")]
        onAccepted: Controller.saveProject(selectedFile)
    }

    footer: Footer {
        logWindow: windowProxy(serverLogLoader)
        problemWindow: windowProxy(problemLogLoader)
    }

    Loader {
        id: serverLogLoader
        active: false
        sourceComponent: ServerLogWindow {}
    }

    Loader {
        id: problemLogLoader
        active: false
        sourceComponent: ProblemLogWindow {}
    }

    Loader {
        id: uiSettingsLoader
        active: false
        sourceComponent: UiSettingsWindow {}
    }

    Loader {
        id: outputSettingsLoader
        active: false
        sourceComponent: OutputSettingsWindow {}
    }

    Loader {
        id: verificationSettingsLoader
        active: false
        sourceComponent: VerificationSettingsWindow {}
    }

    Loader {
        id: runtimeSettingsLoader
        active: false
        sourceComponent: RuntimeSettingsWindow {}
    }

    Loader {
        id: taskModelsSettingsLoader
        active: false
        sourceComponent: TaskModelsWindow {
            runtimeSettingsRef: windowProxy(runtimeSettingsLoader)
        }
    }

    Loader {
        id: ocrModelSettingsLoader
        active: false
        sourceComponent: ModelSettingsWindow {
            role: "pageOcr"
            runtimeSettingsRef: windowProxy(runtimeSettingsLoader)
        }
    }

    Loader {
        id: decisionModelSettingsLoader
        active: false
        sourceComponent: ModelSettingsWindow {
            role: "decision"
            runtimeSettingsRef: windowProxy(runtimeSettingsLoader)
        }
    }

    Loader {
        id: checkModelSettingsLoader
        active: false
        sourceComponent: ModelSettingsWindow {
            role: "blockRecognition"
            runtimeSettingsRef: windowProxy(runtimeSettingsLoader)
        }
    }

    Loader {
        id: layoutModelSettingsLoader
        active: false
        sourceComponent: ModelSettingsWindow {
            role: "layout"
            runtimeSettingsRef: windowProxy(runtimeSettingsLoader)
        }
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

    Loader {
        id: setupWizardLoader
        active: false
        sourceComponent: SetupWizard {}
    }

    Timer {
        id: setupTrigger
        interval: 400
        repeat: false
        onTriggered: {
            if (Settings.setupVersion === 0 && !Settings.setupDismissed)
                mainWindow.openSetupWizard()
        }
    }
    Component.onCompleted: {
        setupTrigger.start()
    }
}
