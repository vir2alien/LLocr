pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window

    title: qsTr("Models by task")
    width: 720
    height: 780
    minimumWidth: 640
    minimumHeight: 640
    modality: Qt.NonModal

    property var runtimeSettingsRef: null

    readonly property var taskIds: ["pageOcr", "rawPageOcr", "blockRecognition", "decision", "layout", "fix"]
    readonly property bool externalMode: Settings.connectionMode === "external"

    // The table edits a draft; Save applies it, Cancel re-reads the settings.
    // task -> {key: profile id, quantId, modelName}.
    property var draft: ({})

    // The row whose Launch/Request tabs are shown below the table.
    property string selectedTask: "pageOcr"

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    function fmtBytes(bytes) {
        if (bytes <= 0)
            return ""
        if (bytes >= 1073741824)
            return (bytes / 1073741824).toFixed(1) + " GiB"
        if (bytes >= 1048576)
            return (bytes / 1048576).toFixed(1) + " MiB"
        return (bytes / 1024).toFixed(1) + " KiB"
    }

    // Draft quant id for a task's profile: the row's own default — the saved
    // choice, else the active quant, else the first installed one (the same
    // order the models list uses). Empty in the external mode or when the
    // profile has no quantization rows.
    function draftQuantId(task, key) {
        if (window.externalMode)
            return ""
        const row = RoleProfiles.quantModels(task).rowFor(key)
        const quants = row.quants || []
        const idx = row.selected >= 0 && row.selected < quants.length ? row.selected : 0
        return quants[idx] ? quants[idx].id : ""
    }

    function syncDraft() {
        var next = {}
        for (const task of window.taskIds) {
            const key = RoleProfiles.requestProfileId(task)
            next[task] = {
                key: key,
                quantId: window.draftQuantId(task, key),
                modelName: Settings.modelNameForRole(task)
            }
        }
        window.draft = next
    }

    function loadValues() {
        window.syncDraft()
        launchTab.loadValues()
        requestTab.loadValues()
    }

    onVisibleChanged: {
        if (visible) {
            ModelInstaller.refreshInstalled()
            ModelInstaller.reloadPresets()
            ModelInstaller.rescanRegistry()
            window.loadValues()
        }
    }

    background: SettingsSurface {
    }

    footer: SettingsFooter {
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            // The queue's progress lives here (the rework plan's stage 6):
            // one install at a time, cancellable as a whole.
            InstallerStatusLabel {
                visible: ModelInstaller.busy
                isError: ModelInstaller.state === ModelInstaller.Error
                busy: ModelInstaller.busy
                statusText: ModelInstaller.statusMessage
            }
            InstallerProgressRow {
                visible: ModelInstaller.busy
                Layout.fillWidth: true
                busy: ModelInstaller.busy
                progress: ModelInstaller.progress
                cancelVisible: ModelInstaller.taskQueueActive
                onCancelClicked: ModelInstaller.cancelTaskInstalls()
            }
        }
        Item { Layout.fillWidth: true }
        LLOButton {
            subtle: true
            text: qsTr("Restore defaults")
            onClicked: {
                launchTab.resetValues()
                requestTab.resetValues()
            }
        }
        LLOButton {
            text: qsTr("Cancel")
            enabled: !ModelInstaller.taskQueueActive
            onClicked: {
                window.loadValues()
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
        launchTab.saveValues()
        requestTab.saveValues()
        if (window.externalMode) {
            for (const task of window.taskIds) {
                RoleProfiles.setRequestProfileId(task, window.draft[task].key)
                Settings.setModelNameForRole(task, window.draft[task].modelName)
            }
            Settings.forceSave()
            window.close()
            return
        }

        var choices = []
        for (const task of window.taskIds) {
            const entry = window.draft[task]
            if (entry.quantId.length > 0)
                choices.push({task: task, key: entry.key, quantId: entry.quantId})
        }
        const missing = ModelInstaller.applyTaskChoices(choices)
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
        // external mode). A click on the task selects it for the tabs below.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Item { Layout.preferredWidth: 148 }
                LLOLabel {
                    Layout.fillWidth: true
                    font.bold: true
                    color: Theme.textMuted
                    text: qsTr("Model")
                }
                Item { Layout.preferredWidth: 190 }
            }

            Repeater {
                model: window.taskIds

                delegate: RowLayout {
                    id: taskRow

                    required property string modelData

                    readonly property var quantModel: RoleProfiles.quantModels(taskRow.modelData)
                    readonly property var entry: window.draft[taskRow.modelData] || {}

                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    Item {
                        id: taskCell
                        Layout.preferredWidth: 148
                        implicitHeight: modelBox.implicitHeight

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -2
                            radius: Theme.controlRadius
                            color: window.selectedTask === taskRow.modelData ? Theme.selected : "transparent"
                        }
                        LLOLabel {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                            elide: Text.ElideRight
                            text: RoleProfiles.title(taskRow.modelData)
                            color: window.selectedTask === taskRow.modelData ? Theme.textPrimary : Theme.textSecondary
                        }
                        TapHandler {
                            onTapped: window.selectedTask = taskRow.modelData
                        }
                    }

                    ComboBox {
                        id: modelBox
                        Layout.fillWidth: true
                        implicitHeight: Theme.controlHeight
                        model: taskRow.quantModel
                        textRole: "title"
                        valueRole: "key"

                        function sync() {
                            const entry = window.draft[taskRow.modelData]
                            if (!entry)
                                return
                            const idx = taskRow.quantModel.rowIndexOf(entry.key)
                            currentIndex = idx >= 0 ? idx : 0
                            if (currentValue)
                                entry.key = currentValue
                            quantBox.sync()
                        }

                        Component.onCompleted: modelBox.sync()
                        onActivated: function (index) {
                            window.draft[taskRow.modelData].key = modelBox.valueAt(index)
                            quantBox.sync()
                        }
                        Connections {
                            target: window
                            function onDraftChanged() { modelBox.sync() }
                        }
                        Connections {
                            target: ModelInstaller
                            function onInstalledChanged() { modelBox.sync() }
                            function onPresetsChanged() { modelBox.sync() }
                        }
                    }

                    ComboBox {
                        id: quantBox
                        visible: !window.externalMode
                        Layout.preferredWidth: 190
                        implicitHeight: Theme.controlHeight
                        textRole: "id"

                        function sync() {
                            const entry = window.draft[taskRow.modelData]
                            if (!entry)
                                return
                            const row = taskRow.quantModel.rowFor(entry.key)
                            model = row.quants || []
                            const idx = model.findIndex(q => q.id.toLowerCase() === String(entry.quantId).toLowerCase())
                            currentIndex = idx >= 0 ? idx : 0
                            if (model[currentIndex])
                                entry.quantId = model[currentIndex].id
                        }

                        Component.onCompleted: quantBox.sync()
                        onActivated: function (index) {
                            window.draft[taskRow.modelData].quantId = quantBox.model[index].id
                        }
                        delegate: ItemDelegate {
                            id: quantDelegate

                            required property var modelData
                            required property int index

                            width: ListView.view.width
                            highlighted: quantDelegate.index === quantBox.currentIndex
                            text: quantDelegate.modelData.id
                                  + (quantDelegate.modelData.installed ? "  \u2713" : "  \u2193")
                                  + (window.fmtBytes(quantDelegate.modelData.size).length > 0
                                     ? "  " + window.fmtBytes(quantDelegate.modelData.size) : "")
                        }
                    }

                    TextField {
                        id: nameField
                        visible: window.externalMode
                        Layout.preferredWidth: 190
                        implicitHeight: Theme.controlHeight
                        text: taskRow.entry.modelName || ""
                        onTextEdited: {
                            if (window.draft[taskRow.modelData])
                                window.draft[taskRow.modelData].modelName = nameField.text
                        }
                    }
                }
            }
        }

        SettingsTabBar {
            id: bottomTabs
            Layout.fillWidth: true
            titles: [qsTr("Launch"), qsTr("Request")]
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: bottomTabs.currentIndex

            LaunchTab {
                id: launchTab
                role: window.selectedTask
                runtimeSettingsRef: window.runtimeSettingsRef
            }

            RequestTab {
                id: requestTab
                role: window.selectedTask
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
                    text: RoleProfiles.title(modelData.task) + " — "
                          + modelData.model + " · " + modelData.quantId
                          + (modelData.size > 0 ? " (" + window.fmtBytes(modelData.size) + ")" : "")
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
}
