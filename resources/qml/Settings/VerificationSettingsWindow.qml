pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    title: qsTr("Verification settings")
    width: 720
    height: 620
    modality: Qt.NonModal

    property bool unsavedChanges: false
    property bool forceClose: false

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    footer: Rectangle {
        implicitHeight: footerRow.implicitHeight + 2 * Theme.spacingLarge
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.divider
        }

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.leftMargin: Theme.paddingWindow
            anchors.rightMargin: Theme.paddingWindow
            anchors.topMargin: Theme.spacingLarge
            anchors.bottomMargin: Theme.spacingLarge
            spacing: Theme.spacing

            LLOButton {
                subtle: true
                text: qsTr("Reset all settings")
                onClicked: {
                    Verification.resetToDefaults()
                    loadAll()
                    window.unsavedChanges = false
                }
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                text: qsTr("Cancel")
                onClicked: {
                    Verification.loadValues()
                    window.unsavedChanges = false
                    window.close()
                }
            }
            LLOButton {
                emphasis: true
                text: qsTr("Save")
                onClicked: {
                    saveAll()
                    Verification.save()
                    window.unsavedChanges = false
                    window.close()
                }
            }
        }//RowLayout
    }//footer

    function loadAll() {
        Verification.loadValues()
        systemTab.loadValues()
        blocksTab.loadValues()
    }

    function saveAll() {
        systemTab.saveValues()
        blocksTab.saveValues()
    }

    onVisibleChanged: {
        if (visible)
            window.loadAll()
    }

    onClosing: (close) => {
        if (window.unsavedChanges && !window.forceClose) {
            close.accepted = false
            closeConfirmDialog.open()
        }
    }

    Connections {
        target: Verification.blockModel
        function onDataChanged() { window.unsavedChanges = true }
    }

    Dialog {
        id: closeConfirmDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 420
        title: qsTr("Discard changes?")
        standardButtons: Dialog.Yes | Dialog.No

        LLOLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            text: qsTr("There are unsaved changes. Close without saving?")
        }

        onAccepted: {
            window.unsavedChanges = false
            window.forceClose = true
            window.close()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
        spacing: Theme.spacing

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            implicitHeight: 32

            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.divider
                }
            }

            component CustomTabButton: TabButton {
                id: tabBtn
                implicitHeight: 32
                padding: 12

                background: Rectangle { color: "transparent" }

                contentItem: Text {
                    text: tabBtn.text
                    font.pointSize: Theme.bodySmallSize
                    font.bold: tabBtn.checked
                    color: tabBtn.checked ? Theme.textPrimary : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                }

                indicator: Rectangle {
                    visible: tabBtn.checked
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    radius: 1
                    color: Theme.accent
                }
            }

            CustomTabButton { text: qsTr("Block checking") }
            CustomTabButton { text: qsTr("System prompt") }
            CustomTabButton { text: qsTr("Block prompts") }
        }//TabBar

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            VerificationBlocksTab {
                id: blocksTab
                onEdited: window.unsavedChanges = true
            }

            VerificationSystemTab {
                id: systemTab
                onEdited: window.unsavedChanges = true
            }

            VerificationPromptsTab {
                id: promptsTab
                onEdited: window.unsavedChanges = true
            }
        }
    }//ColumnLayout
}
