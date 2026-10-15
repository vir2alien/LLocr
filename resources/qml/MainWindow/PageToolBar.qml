pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

ToolBar {
    id: pageToolBarRoot

    signal savePageRequested()

    visible: Controller.hasImage

    leftPadding: Theme.spacing
    rightPadding: Theme.spacing
    topPadding: Theme.spacingSmall
    bottomPadding: Theme.spacingSmall

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

    contentItem: RowLayout {
        spacing: Theme.spacingSmall

        GridLayout {
            rows: 2
            columns: 2
            columnSpacing: 0
            rowSpacing: 0

            SplitToolButton {
                text: qsTr("Recognize page")
                mainEnabled: Controller.hasImage && Controller.actionsIdle
                             && Controller.canRecognize && Controller.pageOcrModelAvailable
                arrowEnabled: Controller.hasImage && Controller.actionsIdle
                onMainActivated: Controller.recognizeCurrentPage("pageOcr")
                onMenuRequested: (anchor) => recognizePageMenu.popup(anchor, 0, anchor.height + 2)
            }

            SplitToolButton {
                text: qsTr("Verify")
                mainEnabled: Controller.hasImage && Controller.actionsIdle
                             && Controller.pageVerificationSupported
                             && Controller.decisionModelAvailable
                             && Controller.fixModelAvailable
                arrowEnabled: mainEnabled
                onMainActivated: Controller.checkEnabledBlocksOnPage(VerificationQueueController.RecheckForce)
                onMenuRequested: (anchor) => verifyPageMenu.popup(anchor, 0, anchor.height + 2)
            }

            ToolButton {
                text: qsTr("Mark up page")
                Layout.minimumWidth: implicitWidth
                enabled: Controller.hasImage && Controller.actionsIdle
                         && Controller.layoutAvailable && Controller.layoutModelAvailable
                onClicked: markupConfirmDialog.requestMarkup()
            }

            ToolButton {
                text: qsTr("Save page")
                Layout.minimumWidth: implicitWidth
                enabled: Controller.hasResult && !Controller.exporting && !Controller.importing
                         && !Controller.checkBusy
                onClicked: pageToolBarRoot.savePageRequested()
            }
        }

        ToolSeparator {}

        GridLayout {
            rows: 2
            columns: 3
            columnSpacing: 0
            rowSpacing: 0

            StubIconButton { iconName: "type" }
            StubIconButton { iconName: "image" }
            StubIconButton { iconName: "table" }
            StubIconButton { iconName: "sigma" }
            StubIconButton { iconName: "code" }

            ToolButton {
                id: allBlocksButton
                implicitWidth: 32
                implicitHeight: 28
                Layout.minimumWidth: implicitWidth
                padding: 0
                Row {
                    anchors.centerIn: parent
                    spacing: 0
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "layout-grid"
                        size: 18
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "\u25be"
                        font: Theme.caption
                        color: Theme.textPrimary
                    }
                }
                onClicked: allBlocksMenu.popup(allBlocksButton, 0, allBlocksButton.height + 2)
            }
        }

        ToolSeparator {}

        Row {
            spacing: 0
            StubIconButton { iconName: "rotate-ccw" }
            StubIconButton { iconName: "rotate-cw" }
        }

        Item { Layout.fillWidth: true }
    }

    Menu {
        id: recognizePageMenu

        MenuItem {
            text: qsTr("Without markup")
            enabled: Controller.hasImage && Controller.actionsIdle
                     && Controller.canRecognize && Controller.rawPageOcrModelAvailable
            onTriggered: Controller.recognizeCurrentPage("rawPageOcr")
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Problem blocks")
            enabled: Controller.hasImage && Controller.actionsIdle
                     && Controller.pageProblemRecognitionSupported
                     && Controller.blockRecognitionModelAvailable
            onTriggered: Controller.recognizeProblemBlocksOnPage()
        }
        MenuItem {
            text: qsTr("All blocks")
            enabled: Controller.hasImage && Controller.actionsIdle
                     && Controller.pageBlocksRecognitionSupported
                     && Controller.blockRecognitionModelAvailable
            onTriggered: Controller.recognizeAllBlocksOnPage()
        }
    }

    Menu {
        id: verifyPageMenu

        MenuItem {
            text: qsTr("Verify only")
            enabled: Controller.hasImage && Controller.actionsIdle
                     && Controller.pageVerificationSupported
                     && Controller.decisionModelAvailable
            onTriggered: Controller.checkEnabledBlocksOnPage(VerificationQueueController.RecheckSkip)
        }
    }

    Menu {
        id: allBlocksMenu

        Instantiator {
            model: Object.keys(BlockNames.nameMap)
            delegate: MenuItem {
                required property string modelData
                text: BlockNames.displayName(modelData)
                enabled: false
            }
            onObjectAdded: (index, object) => allBlocksMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => allBlocksMenu.removeItem(object)
        }
    }

    // The markup pass replaces the page's blocks (and drops its text), so a
    // page that already carries results asks first.
    InterruptConfirmDialog {
        id: markupConfirmDialog

        function requestMarkup() {
            if (Controller.currentPageEditable) {
                title = qsTr("Mark up?")
                prompt = qsTr("Marking up replaces the recognized blocks and text on this page. Continue?")
                open()
            } else {
                Controller.layoutCurrentPage()
            }
        }

        onConfirmed: Controller.layoutCurrentPage()
    }

    component StubIconButton: ToolButton {
        id: stubButton

        property string iconName: ""

        implicitWidth: 32
        implicitHeight: 28
        Layout.minimumWidth: implicitWidth
        padding: 0
        enabled: false

        Icon {
            anchors.centerIn: parent
            name: stubButton.iconName
            size: 18
            color: Theme.textMuted
        }

        ToolTip.visible: stubButton.hovered
        ToolTip.text: qsTr("Not implemented yet")
    }
}
