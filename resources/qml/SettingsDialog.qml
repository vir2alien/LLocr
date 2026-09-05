import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr

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
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: dialog.title
            font.bold: true
            font.pixelSize: Theme.fontNormal
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
        loadValues();//todo через индекс
    }

    function loadValues() {
        uiTab.loadValues();
        modelTab.loadValues();
        runtimeTab.loadValues();

        // Output / parser
        var idx = parserBox.model.indexOf(Settings.parserId)
        parserBox.currentIndex = idx >= 0 ? idx : 0
    }

    onAboutToShow: {
        loadValues()
    }

    onReset: {
        Settings.resetToDefaults();
        loadValues();
    }

    onAccepted: {
        uiTab.savaValues();
        modelTab.saveValues();
        runtimeTab.saveValues();

        Settings.parserId = parserBox.currentText;
        Settings.forceSave();
        I18n.setLanguage(Settings.language);
    }

    Connections {
        target: RuntimeInstaller
        function onCatalogChanged() {
            var r = []
            for (var ri = 0; ri < RuntimeInstaller.releaseCount; ri++)
                r.push(RuntimeInstaller.releaseLabel(ri))
            releaseOptions = r
            if (releaseBox)
                releaseBox.currentIndex = RuntimeInstaller.selectedRelease
        }
        function onBackendChanged() {
            if (backendBox) {
                var idx = RuntimeInstaller.availableBackends.indexOf(RuntimeInstaller.backend)
                backendBox.currentIndex = idx >= 0 ? idx : 0
            }
        }
        function onInstalledChanged() { }
    }

    ColumnLayout {
        clip: true
        anchors.fill: parent
        spacing: 10

        SDTabBar {
            id: tabBar
            Layout.fillWidth: true
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            UITab {
                id: uiTab
            }

            ColumnLayout { // Tab 1 — Connection
                spacing: 4


                Item { Layout.fillHeight: true }
            }

            ModelTab {
                id: modelTab
            }

            ColumnLayout {
                spacing: 4
                Label {
                    text: qsTr("Output parser")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                }
                ComboBox {
                    id: parserBox
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    model: controller.parserNames
                }

                Item { implicitHeight: 6 }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    text: qsTr("‘raw’ keeps the model text as-is. ‘det_tokens’ extracts "
                               + "positioned fragments (bounding boxes) for the overlay.")
                }

                Item { Layout.fillHeight: true }
            }

            RuntimeTab {
                id: runtimeTab
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            ModelsTab {
                Layout.fillWidth: true
                Layout.fillHeight: true
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
