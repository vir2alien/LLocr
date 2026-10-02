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

    background: SettingsSurface {
    }

    footer: SettingsFooter {
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

        SettingsTabBar {
            id: tabBar
            Layout.fillWidth: true
            titles: [qsTr("Block checking"), qsTr("System prompt"), qsTr("Block prompts")]
        }

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
                onEdited: window.unsavedChanges = true
            }
        }
    }//ColumnLayout
}
