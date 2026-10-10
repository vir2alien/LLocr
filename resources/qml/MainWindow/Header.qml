pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

ToolBar {
    id: headerRoot

    signal openFileRequested()
    signal openProjectRequested()
    signal saveProjectRequested()
    signal saveProjectAsRequested()
    signal exportRequested(bool multiPage)
    signal setupWizardRequested()
    signal openUiSettingsRequested()
    signal openOutputSettingsRequested()
    signal openVerificationSettingsRequested()
    signal openRuntimeSettingsRequested()
    signal openOcrModelSettingsRequested()
    signal openDecisionModelSettingsRequested()
    signal openCheckModelSettingsRequested()
    signal openLayoutModelSettingsRequested()

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
            id: fileButton
            text: qsTr("File")
            onClicked: fileMenu.popup(fileButton, 0, fileButton.height + 2)
        }

        ToolSeparator {}

        Row {
            spacing: 0
            ToolButton {
                text: qsTr("Recognize")
                enabled: Controller.hasImage && !Controller.busy
                         && Controller.canRecognize
                onClicked: Controller.recognizeCurrent()
            }
            ToolButton {
                id: recognizeExtraButton
                text: "\u25be"
                enabled: Controller.hasImage && !Controller.busy && !Controller.checkBusy && !Controller.layoutBusy
                onClicked: recognizeExtraMenu.popup(recognizeExtraButton, 0, recognizeExtraButton.height + 2)
            }
            Menu {
                id: recognizeExtraMenu
                MenuItem {
                    text: qsTr("Recognize problem blocks on the page")
                    enabled: Controller.hasImage && !Controller.busy && !Controller.checkBusy
                             && Controller.pageProblemRecognitionSupported
                    onTriggered: Controller.recognizeProblemBlocksOnPage()
                }
                MenuItem {
                    // The layout-pass blocks: no text yet, waiting for their
                    // blockRecognition request.
                    text: qsTr("Recognize all blocks on the page")
                    enabled: Controller.hasImage && !Controller.busy && !Controller.checkBusy && !Controller.layoutBusy
                             && Controller.pageBlocksRecognitionSupported
                    onTriggered: Controller.recognizeAllBlocksOnPage()
                }
            }
        }

        Row {
            spacing: 0
            ToolButton {
                text: qsTr("Recognize all")
                enabled: Controller.hasImage && !Controller.busy
                         && Controller.pageCount > 1
                         && Controller.canRecognize
                onClicked: Controller.recognizeAll()
            }
            ToolButton {
                id: recognizeAllExtraButton
                text: "\u25be"
                enabled: Controller.hasImage && !Controller.busy && !Controller.checkBusy && !Controller.layoutBusy
                onClicked: recognizeAllExtraMenu.popup(recognizeAllExtraButton, 0, recognizeAllExtraButton.height + 2)
            }
            Menu {
                id: recognizeAllExtraMenu
                MenuItem {
                    text: qsTr("Recognize all problem blocks")
                    enabled: Controller.hasImage && !Controller.busy && !Controller.checkBusy && !Controller.layoutBusy
                             && Controller.allProblemRecognitionSupported
                    onTriggered: Controller.recognizeAllProblemBlocks()
                }
            }
        }

        Row {
            spacing: 0
            ToolButton {
                text: qsTr("Mark up")
                enabled: Controller.hasImage && !Controller.busy
                         && !Controller.checkBusy && !Controller.layoutBusy
                         && Controller.layoutAvailable
                onClicked: markupConfirmDialog.requestMarkup(false)
            }
            ToolButton {
                id: markupExtraButton
                text: "\u25be"
                enabled: Controller.hasImage && !Controller.busy
                         && !Controller.checkBusy && !Controller.layoutBusy
                         && Controller.layoutAvailable
                onClicked: markupExtraMenu.popup(markupExtraButton, 0, markupExtraButton.height + 2)
            }
            Menu {
                id: markupExtraMenu
                MenuItem {
                    text: qsTr("Mark up all pages")
                    enabled: Controller.pageCount > 1
                    onTriggered: markupConfirmDialog.requestMarkup(true)
                }
            }
        }

        ToolButton {
            text: qsTr("Check page")
            enabled: Controller.hasImage && !Controller.busy && !Controller.layoutBusy
                     && Controller.pageVerificationSupported
            onClicked: Controller.checkEnabledBlocksOnPage()
        }
        ToolButton {
            text: qsTr("Check all")
            enabled: Controller.hasImage && !Controller.busy && !Controller.layoutBusy
                     && Controller.allPageVerificationSupported
            onClicked: Controller.checkAllEnabledBlocks()
        }
        ToolButton {
            text: qsTr("Stop")
            enabled: Controller.busy || Controller.checkBusy || Controller.layoutBusy
            onClicked: {
                if (Controller.busy)
                    Controller.stop()
                else if (Controller.layoutBusy)
                    Controller.stopLayout()
                else
                    Controller.stopCheck()
            }
        }

        ToolSeparator { visible: Controller.pageCount > 1 }

        RowLayout {
            visible: Controller.pageCount > 1
            spacing: 0

            ToolButton {
                text: "\u2039"
                enabled: Controller.currentPage > 0
                onClicked: Controller.currentPage = Controller.currentPage - 1
            }
            LLOLabel {
                text: (Controller.currentPage + 1) + " / " + Controller.pageCount
                horizontalAlignment: Text.AlignHCenter
                Layout.minimumWidth: 56
            }
            ToolButton {
                text: "\u203a"
                enabled: Controller.currentPage < Controller.pageCount - 1
                onClicked: Controller.currentPage = Controller.currentPage + 1
            }
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            id: settingsButton
            text: qsTr("Settings")
            onClicked: settingsMenu.popup(settingsButton, 0, settingsButton.height + 2)
        }
    }//RowLayout

    Menu {
        id: fileMenu

        MenuItem {
            text: qsTr("Open…")
            enabled: !Controller.busy && !Controller.importing && !Controller.exporting
            onTriggered: headerRoot.openFileRequested()
        }
        MenuItem {
            text: qsTr("Open project…")
            enabled: !Controller.busy && !Controller.importing && !Controller.exporting
                         && !Controller.projectBusy && !Controller.checkRunning
            onTriggered: headerRoot.openProjectRequested()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Save project")
            enabled: Controller.hasImage && !Controller.busy && !Controller.importing
                         && !Controller.exporting && !Controller.projectBusy
                         && !Controller.checkRunning
            onTriggered: headerRoot.saveProjectRequested()
        }
        MenuItem {
            text: qsTr("Save project as…")
            enabled: Controller.hasImage && !Controller.busy && !Controller.importing
                         && !Controller.exporting && !Controller.projectBusy
                         && !Controller.checkRunning
            onTriggered: headerRoot.saveProjectAsRequested()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Export…")
            enabled: Controller.hasResult && !Controller.exporting && !Controller.importing
            onTriggered: {
                if (Controller.pageCount > 1) {
                    headerRoot.exportRequested(true)
                } else {
                    headerRoot.exportRequested(false)
                }
            }
        }
    }

    Menu {
        id: settingsMenu

        MenuItem {
            text: qsTr("Setup wizard")
            onTriggered: headerRoot.setupWizardRequested()
        }
        MenuItem {
            text: qsTr("Interface")
            onTriggered: headerRoot.openUiSettingsRequested()
        }
        MenuItem {
            text: qsTr("Output")
            onTriggered: headerRoot.openOutputSettingsRequested()
        }
        MenuItem {
            text: qsTr("Verification")
            onTriggered: headerRoot.openVerificationSettingsRequested()
        }
        MenuItem {
            text: qsTr("Runtime")
            onTriggered: headerRoot.openRuntimeSettingsRequested()
        }
        MenuItem {
            text: qsTr("OCR model")
            onTriggered: headerRoot.openOcrModelSettingsRequested()
        }
        MenuItem {
            text: qsTr("Decision model")
            onTriggered: headerRoot.openDecisionModelSettingsRequested()
        }
        MenuItem {
            text: qsTr("Block OCR model")
            onTriggered: headerRoot.openCheckModelSettingsRequested()
        }
        MenuItem {
            text: qsTr("Layout model")
            onTriggered: headerRoot.openLayoutModelSettingsRequested()
        }
    }

    // The markup pass replaces the page's blocks (and drops its text), so a
    // page that already carries results asks first.
    InterruptConfirmDialog {
        id: markupConfirmDialog

        property bool allPages: false

        function requestMarkup(allPages) {
            markupConfirmDialog.allPages = allPages
            const hasContent = allPages ? Controller.hasResult : Controller.currentPageEditable
            if (hasContent) {
                title = qsTr("Mark up?")
                prompt = allPages ? qsTr("Marking up replaces the recognized blocks and text on every page. Continue?")
                                  : qsTr("Marking up replaces the recognized blocks and text on this page. Continue?")
                open()
            } else if (allPages) {
                Controller.layoutAllPages()
            } else {
                Controller.layoutCurrentPage()
            }
        }

        onConfirmed: {
            if (allPages)
                Controller.layoutAllPages()
            else
                Controller.layoutCurrentPage()
        }
    }
} // ToolBar