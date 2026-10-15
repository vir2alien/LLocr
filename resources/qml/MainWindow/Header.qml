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

    // Fusion's MenuItem draws no shortcut text, so each item with a shortcut
    // carries its own caption; on macOS the stored "Ctrl+" is really Cmd.
    function shortcutDisplayText(sequence) {
        if (Qt.platform.os !== "osx")
            return sequence
        return String(sequence).replace("Ctrl+Shift+", "⇧⌘").replace("Ctrl+", "⌘")
    }

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
            Layout.minimumWidth: implicitWidth
            onClicked: fileMenu.popup(fileButton, 0, fileButton.height + 2)
        }

        ToolSeparator {}

        ToolButton {
            text: qsTr("Recognize")
            Layout.minimumWidth: implicitWidth
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Recognize every page of the document")
            enabled: Controller.hasImage && Controller.actionsIdle && Controller.canRecognize
            onClicked: Controller.recognizeAll()
        }
        ToolButton {
            text: qsTr("Verify")
            Layout.minimumWidth: implicitWidth
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Verify the recognized blocks on every page")
            enabled: Controller.hasImage && !Controller.busy && !Controller.checkBusy
                     && !Controller.layoutBusy
                     && Controller.allPageVerificationSupported
            onClicked: Controller.checkAllEnabledBlocks()
        }
        ToolButton {
            text: qsTr("Fix")
            Layout.minimumWidth: implicitWidth
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Fix the problem blocks in the whole document")
            enabled: Controller.hasImage && !Controller.busy && !Controller.checkBusy
                     && !Controller.layoutBusy && Controller.allProblemRecognitionSupported
            onClicked: Controller.fixAllProblemBlocks()
        }
        ToolButton {
            text: qsTr("Mark up")
            Layout.minimumWidth: implicitWidth
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Mark up every page of the document")
            enabled: Controller.hasImage && !Controller.busy
                     && !Controller.checkBusy && !Controller.layoutBusy
                     && Controller.layoutAvailable
            onClicked: markupConfirmDialog.requestMarkup()
        }
        ToolButton {
            text: qsTr("Export")
            Layout.minimumWidth: implicitWidth
            enabled: Controller.hasResult && !Controller.exporting && !Controller.importing
                     && !Controller.checkBusy
            onClicked: headerRoot.exportRequested(Controller.pageCount > 1)
        }

        ToolButton {
            text: qsTr("Stop")
            Layout.minimumWidth: implicitWidth
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
                Layout.minimumWidth: implicitWidth
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
                Layout.minimumWidth: implicitWidth
                enabled: Controller.currentPage < Controller.pageCount - 1
                onClicked: Controller.currentPage = Controller.currentPage + 1
            }
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            id: settingsButton
            text: qsTr("Settings")
            Layout.minimumWidth: implicitWidth
            onClicked: settingsMenu.popup(settingsButton, 0, settingsButton.height + 2)
        }
    }//RowLayout

    Menu {
        id: fileMenu

        MenuItem {
            id: openItem

            action: Action {
                id: openAction
                text: qsTr("Open…")
                shortcut: "Ctrl+O"
                enabled: !Controller.busy && !Controller.importing && !Controller.exporting
                         && !Controller.checkBusy
                onTriggered: headerRoot.openFileRequested()
            }

            rightPadding: padding + spacing + openShortcutLabel.implicitWidth

            Label {
                id: openShortcutLabel
                anchors.right: parent.right
                anchors.rightMargin: openItem.padding
                anchors.verticalCenter: parent.verticalCenter
                font: Theme.caption
                color: Theme.textMuted
                text: headerRoot.shortcutDisplayText(openAction.shortcut)
            }
        }
        MenuItem {
            text: qsTr("Open project…")
            enabled: !Controller.busy && !Controller.importing && !Controller.exporting
                         && !Controller.projectBusy && !Controller.checkRunning
            onTriggered: headerRoot.openProjectRequested()
        }
        MenuSeparator {}
        MenuItem {
            id: saveItem

            action: Action {
                id: saveAction
                text: qsTr("Save project")
                shortcut: "Ctrl+S"
                enabled: Controller.hasImage && !Controller.busy && !Controller.importing
                         && !Controller.exporting && !Controller.projectBusy
                         && !Controller.checkRunning
                onTriggered: headerRoot.saveProjectRequested()
            }

            rightPadding: padding + spacing + saveShortcutLabel.implicitWidth

            Label {
                id: saveShortcutLabel
                anchors.right: parent.right
                anchors.rightMargin: saveItem.padding
                anchors.verticalCenter: parent.verticalCenter
                font: Theme.caption
                color: Theme.textMuted
                text: headerRoot.shortcutDisplayText(saveAction.shortcut)
            }
        }
        MenuItem {
            id: saveAsItem

            action: Action {
                id: saveAsAction
                text: qsTr("Save project as…")
                shortcut: "Ctrl+Shift+S"
                enabled: Controller.hasImage && !Controller.busy && !Controller.importing
                         && !Controller.exporting && !Controller.projectBusy
                         && !Controller.checkRunning
                onTriggered: headerRoot.saveProjectAsRequested()
            }

            rightPadding: padding + spacing + saveAsShortcutLabel.implicitWidth

            Label {
                id: saveAsShortcutLabel
                anchors.right: parent.right
                anchors.rightMargin: saveAsItem.padding
                anchors.verticalCenter: parent.verticalCenter
                font: Theme.caption
                color: Theme.textMuted
                text: headerRoot.shortcutDisplayText(saveAsAction.shortcut)
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Export…")
            enabled: Controller.hasResult && !Controller.exporting && !Controller.importing
                     && !Controller.checkBusy
            onTriggered: headerRoot.exportRequested(Controller.pageCount > 1)
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
        MenuSeparator {}
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

    // The markup pass replaces the pages' blocks (and drops their text), so a
    // document that already carries results asks first.
    InterruptConfirmDialog {
        id: markupConfirmDialog

        function requestMarkup() {
            if (Controller.hasResult) {
                title = qsTr("Mark up?")
                prompt = qsTr("Marking up replaces the recognized blocks and text on every page. Continue?")
                open()
            } else {
                Controller.layoutAllPages()
            }
        }

        onConfirmed: Controller.layoutAllPages()
    }
} // ToolBar
