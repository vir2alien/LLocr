pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window

    title: qsTr("Models")
    width: 680
    height: 620
    minimumWidth: 680
    minimumHeight: 500
    modality: Qt.NonModal

    property var runtimeSettingsRef: null

    readonly property bool externalMode: Settings.connectionMode === "external"

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    onVisibleChanged: {
        if (visible) {
            ModelInstaller.refreshInstalled()
            ModelInstaller.reloadPresets()
            ModelInstaller.rescanRegistry()
            taskTable.syncDraft()
        }
    }

    background: SettingsSurface {
    }

    footer: SettingsFooter {
        Item { Layout.fillWidth: true }
        LLOButton {
            subtle: true
            text: qsTr("Restore defaults")
            onClicked: taskTable.resetTabs()
        }
        LLOButton {
            text: qsTr("Cancel")
            enabled: !ModelInstaller.taskQueueActive
            onClicked: {
                taskTable.resetDraft()
                window.close()
            }
        }
        LLOButton {
            emphasis: true
            text: qsTr("Save")
            enabled: !ModelInstaller.taskQueueActive
            onClicked: window.save()
        }
    }//footer

    function save() {
        taskTable.saveTabs()
        if (window.externalMode) {
            taskTable.applyExternalDraft()
            window.close()
            return
        }

        const missing = ModelInstaller.applyTaskChoices(taskTable.buildChoices())
        taskTable.markDraftSaved()
        if (missing.length === 0) {
            window.close()
            return
        }
        missingDialog.items = missing
        missingDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
        spacing: 8

        // The six tasks: model + quantization per row (model name in the
        // external mode); the gear opens that task's Launch/Request settings.
        TaskModelsTable {
            id: taskTable
            Layout.fillWidth: true
            runtimeSettingsRef: window.runtimeSettingsRef
            externalMode: window.externalMode
        }

        // The queue's progress (the rework plan's stage 6): one install at a
        // time, cancellable as a whole.
        ColumnLayout {
            Layout.fillWidth: true
            visible: ModelInstaller.busy
            spacing: 4

            InstallerStatusLabel {
                Layout.fillWidth: true
                isError: ModelInstaller.state === ModelInstaller.Error
                busy: ModelInstaller.busy
                statusText: ModelInstaller.statusMessage
            }
            InstallerProgressRow {
                Layout.fillWidth: true
                busy: ModelInstaller.busy
                progress: ModelInstaller.progress
                cancelVisible: ModelInstaller.taskQueueActive
                onCancelClicked: ModelInstaller.cancelTaskInstalls()
            }
        }

        // The files actually on disk: one row per installed model+quant with
        // its size, how many tasks use it and a guarded delete.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSmall

            LLOLabel {
                Layout.fillWidth: true
                font.bold: true
                color: Theme.textMuted
                text: qsTr("Downloaded model files on disk")
            }

            LLOLabel {
                id: filesErrorLabel

                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.WordWrap
                font.pointSize: Theme.captionSize
                color: Theme.error
            }

            ListView {
                id: filesList

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                visible: ModelInstaller.installedCount > 0
                model: ModelInstaller.installedFiles
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: RowLayout {
                    id: fileRow

                    required property string entryId
                    required property string title
                    required property string quant
                    required property string sizeLabel
                    required property int usageCount
                    required property string usageText
                    required property bool removable
                    required property string note

                    width: filesList.width
                    implicitHeight: Theme.controlHeight
                    spacing: Theme.spacingSmall

                    LLOLabel {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: fileRow.title
                        color: Theme.textPrimary
                    }
                    LLOLabel {
                        Layout.preferredWidth: 64
                        text: fileRow.quant
                        color: Theme.textSecondary
                    }
                    LLOLabel {
                        Layout.preferredWidth: 76
                        horizontalAlignment: Text.AlignRight
                        text: fileRow.sizeLabel
                        color: Theme.textSecondary
                    }
                    LLOLabel {
                        Layout.preferredWidth: 190
                        elide: Text.ElideRight
                        text: fileRow.usageText
                        color: fileRow.usageCount > 0 ? Theme.textPrimary : Theme.textMuted
                    }
                    ToolButton {
                        id: openFolderButton

                        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
                        implicitHeight: Math.max(Theme.controlHeight, topPadding + bottomPadding + contentItem.implicitHeight)
                        onClicked: {
                            const err = ModelInstaller.openInstalledEntryFolder(fileRow.entryId)
                            filesErrorLabel.text = err
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Open folder")

                        contentItem: Item {
                            implicitWidth: folderIcon.size
                            implicitHeight: folderIcon.size

                            Icon {
                                id: folderIcon
                                anchors.centerIn: parent
                                name: "folder"
                                size: 15
                                color: Theme.textSecondary
                            }
                        }
                    }
                    ToolButton {
                        id: deleteRowButton

                        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
                        implicitHeight: Math.max(Theme.controlHeight, topPadding + bottomPadding + contentItem.implicitHeight)
                        onClicked: {
                            if (!fileRow.removable) {
                                filesErrorLabel.text = fileRow.note
                                return
                            }
                            deleteFileDialog.entryId = fileRow.entryId
                            deleteFileDialog.subject = fileRow.title + " · " + fileRow.quant
                            deleteFileDialog.usageCount = fileRow.usageCount
                            deleteFileDialog.open()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: fileRow.removable ? qsTr("Delete") : fileRow.note

                        contentItem: Item {
                            implicitWidth: trashIcon.size
                            implicitHeight: trashIcon.size

                            Icon {
                                id: trashIcon
                                anchors.centerIn: parent
                                name: "trash-2"
                                size: 15
                                color: deleteRowButton.enabled ? Theme.textSecondary : Theme.textMuted
                            }
                        }
                    }
                }
            }

            LLOLabel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: ModelInstaller.installedCount === 0
                wrapMode: Text.WordWrap
                font.pointSize: Theme.captionSize
                color: Theme.helpColor
                text: qsTr("Downloaded models will appear here — pick a quantization in the rows above and save.")
            }
        }
    }//ColumnLayout

    // The not-installed quantizations the Save ran into (the rework plan's
    // stage 6): one sequential download queue after the user agrees.
    Dialog {
        id: missingDialog

        property var items: []

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: false
        standardButtons: Dialog.NoButton
        title: qsTr("Models to download")

        ColumnLayout {
            spacing: 4

            Repeater {
                model: missingDialog.items

                delegate: LLOLabel {
                    required property var modelData
                    Layout.fillWidth: true
                    text: RoleProfiles.choiceTitles(modelData) + " — "
                          + modelData.model + " · " + modelData.quantId
                          + (modelData.size > 0 ? " (" + taskTable.fmtBytes(modelData.size) + ")" : "")
                }
            }
        }

        footer: DialogButtonBox {
            LLOButton {
                text: qsTr("Cancel")
                flat: true
                onClicked: missingDialog.close()
            }
            LLOButton {
                text: qsTr("Download")
                emphasis: true
                enabled: !ModelInstaller.busy
                onClicked: {
                    ModelInstaller.queueTaskInstalls(missingDialog.items)
                    missingDialog.close()
                }
            }
        }
    }

    Dialog {
        id: deleteFileDialog

        property string entryId: ""
        property string subject: ""
        property int usageCount: 0

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 420
        title: qsTr("Delete model file")

        onOpened: deleteFileButton.forceActiveFocus()

        contentItem: LLOLabel {
            width: deleteFileDialog.availableWidth
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.textSecondary
            text: deleteFileDialog.usageCount > 0
                  ? qsTr("%1 is used by %2 task(s). The task rows keep their choice, but the model is gone from disk until you download it again.").arg(deleteFileDialog.subject).arg(deleteFileDialog.usageCount)
                  : qsTr("Delete %1 from disk?").arg(deleteFileDialog.subject)
        }

        footer: Item {
            implicitHeight: buttons.implicitHeight + Theme.spacingLarge

            RowLayout {
                id: buttons
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.spacingLarge
                anchors.rightMargin: Theme.spacingLarge
                anchors.bottomMargin: Theme.spacing
                spacing: Theme.spacing

                Item { Layout.fillWidth: true }
                LLOButton {
                    text: qsTr("Cancel")
                    onClicked: deleteFileDialog.close()
                }
                LLOButton {
                    id: deleteFileButton
                    text: qsTr("Delete")
                    emphasis: true
                    Keys.onEscapePressed: deleteFileDialog.close()
                    onClicked: {
                        const err = ModelInstaller.removeInstalledEntry(deleteFileDialog.entryId)
                        deleteFileDialog.close()
                        filesErrorLabel.text = err
                    }
                }
            }
        }
    }
}
