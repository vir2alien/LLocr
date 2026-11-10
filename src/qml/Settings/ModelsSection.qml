pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// The models section: the task rows (model and quantization per task), the
// download queue and the files on disk. It is a draft — the host applies it
// on save (ADR 297).
Item {
    id: root

    signal edited()
    // The popup of a task offers the runtime settings: in the unified window
    // that is the "Runtime" section.
    signal runtimeSectionRequested()

    readonly property bool externalMode: Settings.connectionMode === "external"

    function loadValues() {
        ModelInstaller.refreshInstalled()
        ModelInstaller.reloadPresets()
        ModelInstaller.rescanRegistry()
        taskTable.syncDraft()
    }

    function saveValues() {
        taskTable.saveTabs()
        if (root.externalMode) {
            taskTable.applyExternalDraft()
            return true
        }

        const missing = ModelInstaller.applyTaskChoices(taskTable.buildChoices())
        taskTable.markDraftSaved()
        if (missing.length === 0)
            return true

        missingDialog.items = missing
        missingDialog.open()
        return false
    }

    function cancelValues() {
        taskTable.resetDraft()
    }

    function resetValues() {
        taskTable.resetTabs()
        taskTable.resetDraft()
        root.loadValues()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        TaskModelsTable {
            id: taskTable
            Layout.fillWidth: true
            externalMode: root.externalMode
            onEdited: root.edited()
            onRuntimeSectionRequested: root.runtimeSectionRequested()
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: ModelInstaller.busy
            spacing: Theme.spacingSmall

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

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            LLOLabel {
                Layout.fillWidth: true
                font.pixelSize: Theme.bodySize
                color: Theme.textPrimary
                text: qsTr("Downloaded model files on disk")
            }

            LLOLabel {
                id: filesErrorLabel

                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.footnoteSize
                color: Theme.error
            }

            ListView {
                id: filesList

                Layout.fillWidth: true
                implicitHeight: Math.min(contentHeight, 300)
                clip: true
                spacing: Theme.spacingTiny
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
                        wrapMode: Text.NoWrap
                        font.pixelSize: Theme.bodySmallSize
                        text: fileRow.title
                        color: Theme.textPrimary
                    }
                    LLOLabel {
                        Layout.preferredWidth: 64
                        font.pixelSize: Theme.bodySmallSize
                        text: fileRow.quant
                        color: Theme.textSecondary
                    }
                    LLOLabel {
                        Layout.preferredWidth: 76
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: Theme.bodySmallSize
                        text: fileRow.sizeLabel
                        color: Theme.textSecondary
                    }
                    LLOLabel {
                        Layout.preferredWidth: 165
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        font.pixelSize: Theme.bodySmallSize
                        text: fileRow.usageText
                        color: fileRow.usageCount > 0 ? Theme.textPrimary : Theme.textMuted
                    }
                    ToolButton {
                        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
                        implicitHeight: Math.max(Theme.controlHeight, topPadding + bottomPadding + contentItem.implicitHeight)
                        onClicked: filesErrorLabel.text = ModelInstaller.openInstalledEntryFolder(fileRow.entryId)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Open folder")

                        contentItem: Item {
                            implicitWidth: folderIcon.size
                            implicitHeight: folderIcon.size

                            Icon {
                                id: folderIcon
                                anchors.centerIn: parent
                                name: "folder"
                                size: Theme.iconSizeMedium
                                color: Theme.textSecondary
                            }
                        }
                    }
                    ToolButton {
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
                                size: Theme.iconSizeMedium
                                color: parent.enabled ? Theme.textSecondary : Theme.textMuted
                            }
                        }
                    }
                }
            }

            LLOLabel {
                Layout.fillWidth: true
                visible: ModelInstaller.installedCount === 0
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.footnoteSize
                color: Theme.helpColor
                text: qsTr("Downloaded models will appear here — pick a quantization in the rows above and save.")
            }
        }
    }

    Dialog {
        id: missingDialog

        property var items: []

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: false
        standardButtons: Dialog.NoButton
        title: qsTr("Models to download")

        ColumnLayout {
            spacing: Theme.spacingSmall

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
            font.pixelSize: Theme.captionSize
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
