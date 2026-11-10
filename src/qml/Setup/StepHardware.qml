pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// The wizard's hardware step (ADR 170): the detected GPU/memory, the runtime
// backend it implies (still changeable) and the per-task model table
// pre-filled with the profile defaults (ADR 169) and memory-fitting quants.
Item {
    id: root

    property bool complete: true

    // The missing quantizations after applyChoices() — the wizard hands them
    // to the check step.
    property var missingChoices: []

    // The tabs of the gear popup: false when one of them asked the user a
    // question and wrote nothing.
    property bool tabsWritten: true

    function applyChoices() {
        tabsWritten = taskTable.saveTabs()
        if (!tabsWritten)
            return missingChoices
        missingChoices = ModelInstaller.applyTaskChoices(taskTable.buildChoices())
        taskTable.markDraftSaved()
        return missingChoices
    }

    implicitHeight: contentColumn.implicitHeight + 40

    onVisibleChanged: {
        if (!visible)
            return
        ModelInstaller.refreshInstalled()
        ModelInstaller.reloadPresets()
        ModelInstaller.rescanRegistry()
        taskTable.syncDraft()
        // Tasks the user has never configured get the auto-selection; already
        // configured tasks keep their own pick. The request profile id cannot
        // tell the two apart — every task has one by default — so the model
        // the task points at is the sign.
        const prefill = ModelInstaller.defaultTaskChoices().filter(
                    choice => Settings.launchModelPathForRole(choice.task).length === 0)
        taskTable.prefillChoices(prefill)
    }

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 20
        spacing: Theme.spacing

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Hardware and models")
            font.pixelSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("The app has detected your hardware and picked the runtime "
                       + "backend and default models — a quantization that fits "
                       + "the memory. Change anything below; the check runs on "
                       + "the last step.")
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            LLOLabel {
                text: qsTr("Detected hardware")
                font.bold: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                LLOLabel {
                    Layout.fillWidth: true
                    text: HardwareInfo.gpuName.length > 0 ? HardwareInfo.gpuName : qsTr("CPU (no GPU detected)")
                }
                LLOLabel {
                    text: (HardwareInfo.gpuName.length > 0 ? qsTr("VRAM") : qsTr("RAM"))
                          + ": " + HardwareInfo.memoryLabel
                    color: Theme.textSecondary
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            LLOLabel {
                text: qsTr("Runtime backend")
                font.bold: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                LLOComboBox {
                    id: backendBox

                    Layout.preferredWidth: 220
                    model: RuntimeInstaller.availableBackends
                    displayText: RuntimeInstaller.backendDisplayName(RuntimeInstaller.backend)

                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        font: Theme.button
                        width: backendBox.width
                        height: Theme.controlHeight
                        highlighted: backendBox.highlightedIndex === index
                        text: RuntimeInstaller.backendDisplayName(modelData)
                        onTriggered: RuntimeInstaller.backend = modelData
                    }

                    function sync() {
                        currentIndex = RuntimeInstaller.availableBackends.indexOf(RuntimeInstaller.backend)
                    }

                    Component.onCompleted: sync()
                    onActivated: function (index) {
                        RuntimeInstaller.backend = RuntimeInstaller.availableBackends[index]
                    }
                    Connections {
                        target: RuntimeInstaller
                        function onBackendChanged() { backendBox.sync() }
                    }
                }

                LLOLabel {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 340
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.captionSize
                    color: Theme.textSecondary
                    text: RuntimeInstaller.recommendationReason
                }
            }

            LLOLabel {
                font.pixelSize: Theme.captionSize
                color: Theme.textMuted
                text: RuntimeInstaller.platformLabel
            }
        }

        TaskModelsTable {
            id: taskTable
            Layout.fillWidth: true
            externalMode: false
            showGear: true
            // The wizard has no runtime section to show: the button that
            // asks for one would lead nowhere.
            runtimeSectionAvailable: false
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: !root.tabsWritten
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.footnoteSize
            color: Theme.error
            text: qsTr("The launch parameters are written only after you confirm the value that looks like a secret. Answer the question and press Next again.")
        }

        Item { Layout.fillHeight: true }
    }
}
