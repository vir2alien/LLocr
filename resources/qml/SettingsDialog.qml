import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr

import "Common"
import "SettingsDialog"

Dialog {
    id: dialog
    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Save | Dialog.RestoreDefaults | Dialog.Cancel

    property bool canManage: !Runtime.lockedOut
                             && Settings.serverPath.trim().length > 0

    property var setupWizardRef: null

    property var logWindowRef: null

    enum TabsEnum {UiTabNum = 0, RequesetTabNum = 1, OutputTabNum = 2, RuntimeTabNum = 3, LaunchTabNum = 4, ModelsTabNum = 5}

    anchors.centerIn: parent
    width: 520
    implicitHeight: 600

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    header: Item {
        implicitHeight: 38
        LLOLabel {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: dialog.title
            font.bold: true
            color: Theme.textPrimary
        }
    }

    footer: DialogButtonBox {
        background: Rectangle {
            color: "transparent"
        }
        alignment: Qt.AlignRight
        standardButtons: dialog.standardButtons
        spacing: 6
        padding: 10
    }

    function selectTab(index) {
        tabBar.currentIndex = index;
        loadTab(index);
    }

    function loadTab(index) {
        switch (index) {
        case SettingsDialog.TabsEnum.UiTabNum:
            uiTab.loadValues(); break;
        case SettingsDialog.TabsEnum.RequesetTabNum:
            requestTab.loadValues(); break;
        case SettingsDialog.TabsEnum.OutputTabNum: {
            var idx = parserBox.model.indexOf(Settings.parserId)
            parserBox.currentIndex = idx >= 0 ? idx : 0
            break;
        }
        case SettingsDialog.TabsEnum.RuntimeTabNum:
            Runtime.refreshSingleInstanceLock()
            runtimeTab.loadValues(); break;
        case SettingsDialog.TabsEnum.LaunchTabNum:
            launchTab.loadValues(); break;
        case SettingsDialog.TabsEnum.ModelsTabNum:
            break;
        }
    }

    onAboutToShow: {
        uiTab.loadValues()
        requestTab.loadValues()
        runtimeTab.loadValues()
        launchTab.loadValues()
        loadTab(tabBar.currentIndex)
    }

    onReset: {
        Settings.resetToDefaults();
        loadTab(tabBar.currentIndex);
        // After loadTab: the Request/Launch drafts must end up as the default
        // profiles, not re-loaded from the persisted user profiles.
        requestTab.resetValues();
        launchTab.resetValues();
    }

    onAccepted: {
        uiTab.savaValues();
        requestTab.saveValues();
        runtimeTab.saveValues();
        launchTab.saveValues();

        Settings.parserId = parserBox.currentText;
        Settings.forceSave();
        I18n.setLanguage(Settings.language);
    }

    ColumnLayout {
        clip: true
        anchors.fill: parent
        spacing: 10

        SDTabBar {
            id: tabBar
            Layout.fillWidth: true
            onCurrentIndexChanged: loadTab(currentIndex)
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            UITab {
                id: uiTab
            }

            RequestTab {
                id: requestTab
            }

            ColumnLayout {
                spacing: 4
                LLOLabel {
                    text: qsTr("Output parser")
                }
                ComboBox {
                    id: parserBox
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    model: controller.parserNames
                }

                Item { implicitHeight: 6 }

                LLOLabel {
                    Layout.fillWidth: true
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: qsTr("‘raw’ keeps the model text as-is. ‘det_tokens’ extracts "
                               + "positioned fragments (bounding boxes) for the overlay.")
                }

                Item { Layout.fillHeight: true }
            }

            RuntimeTab {
                id: runtimeTab
            }

            LaunchTab {
                id: launchTab
            }

            ModelsTab {
                id: modelSelectTab
            }
        }
    }

    FileDialog {
        id: serverPicker
        title: qsTr("Select llama-server binary")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Executables (*)")]
        onAccepted: {
            const path = Runtime.localPath(selectedFile)
            Settings.serverPath = path
            serverPathField.text = path
            Runtime.probeRuntimePath(path)
        }
    }
}
