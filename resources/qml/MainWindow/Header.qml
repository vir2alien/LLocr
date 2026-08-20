import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ToolBar {
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

        ToolButton {
            text: qsTr("Settings...")
            onClicked: settingsDialog.open()
        }
    }
} // ToolBar