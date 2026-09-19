pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

ToolBar {
    id: headerRoot

    signal openFileRequested()
    signal exportRequested(bool multiPage)
    signal openUiSettingsRequested()
    signal openOutputSettingsRequested()
    signal openRuntimeSettingsRequested()
    signal openOcrModelSettingsRequested()

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
            enabled: !Controller.busy && !Controller.importing && !Controller.exporting
            onClicked: headerRoot.openFileRequested()
        }

        ToolSeparator {}

        ToolButton {
            text: qsTr("Recognize")
            enabled: Controller.hasImage && !Controller.busy
                     && Controller.canRecognize
            onClicked: Controller.recognizeCurrent()
        }
        ToolButton {
            text: qsTr("Recognize all")
            enabled: Controller.hasImage && !Controller.busy
                     && Controller.pageCount > 1
                     && Controller.canRecognize
            onClicked: Controller.recognizeAll()
        }
        ToolButton {
            text: qsTr("Stop")
            enabled: Controller.busy
            onClicked: Controller.stop()
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

        ToolSeparator {}

        ToolButton {
            text: qsTr("Export…")
            enabled: Controller.hasResult && !Controller.exporting && !Controller.importing
            onClicked: {
                if (Controller.pageCount > 1) {
                    headerRoot.exportRequested(true)
                } else {
                    headerRoot.exportRequested(false)
                }
            }
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            id: settingsButton
            text: qsTr("Settings")
            onClicked: settingsMenu.popup(settingsButton, 0, settingsButton.height + 2)
        }
    }

    Menu {
        id: settingsMenu

        MenuItem {
            text: qsTr("Interface")
            onTriggered: headerRoot.openUiSettingsRequested()
        }
        MenuItem {
            text: qsTr("Output")
            onTriggered: headerRoot.openOutputSettingsRequested()
        }
        MenuItem {
            text: qsTr("Runtime")
            onTriggered: headerRoot.openRuntimeSettingsRequested()
        }
        MenuItem {
            text: qsTr("OCR model")
            onTriggered: headerRoot.openOcrModelSettingsRequested()
        }
    }
} // ToolBar