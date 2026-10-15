pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window

    title: qsTr("Models")
    width: 760
    height: 780
    minimumWidth: 680
    minimumHeight: 620
    modality: Qt.NonModal

    property var runtimeSettingsRef: null

    readonly property var taskIds: ["pageOcr", "rawPageOcr", "blockRecognition", "decision", "layout", "fix"]
    readonly property bool externalMode: Settings.connectionMode === "external"

    // The table edits a draft; Save applies it, Cancel re-reads the settings.
    // task -> {key: profile id, quantId, modelName}.
    property var draft: ({})

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

    // Draft quant id for a task's profile: the row's own default — the active
    // quant, else the saved choice, else the first installed one (the same
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
    }

    function openTaskSettings(task, button) {
        taskSettingsPopup.task = task
        taskSettingsPopup.attachTo(button)
        taskSettingsPopup.open()
    }

    function quantMarker(q) {
        return q.installed ? "✓" : (q.downloadable ? "⬇" : "·")
    }

    function quantItemText(q) {
        const status = q.installed ? qsTr("Installed") : (q.downloadable ? qsTr("Download") : "")
        let s = window.quantMarker(q) + " " + q.id
        if (q.sizeLabel.length > 0)
            s += " — " + q.sizeLabel
        const tail = status.length > 0 && q.hint.length > 0 ? status + ", " + q.hint
                                                                 : (status.length > 0 ? status : q.hint)
        if (tail.length > 0)
            s += " (" + tail + ")"
        return s
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
        // external mode); the gear opens that task's Launch/Request settings.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Item { Layout.preferredWidth: 200 }
                LLOLabel {
                    Layout.fillWidth: true
                    font.bold: true
                    color: Theme.textMuted
                    text: qsTr("Model")
                }
                Item { Layout.preferredWidth: 150 }
                Item { Layout.preferredWidth: 36 }
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
                        Layout.preferredWidth: 200
                        implicitHeight: modelBox.implicitHeight

                        LLOLabel {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingSmall
                            elide: Text.ElideRight
                            text: RoleProfiles.title(taskRow.modelData)
                            color: Theme.textSecondary
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
                            // The saved quant choice belongs to the previous
                            // profile; the new row picks its own default.
                            window.draft[taskRow.modelData].quantId = ""
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
                        Layout.preferredWidth: 150
                        implicitHeight: Theme.controlHeight
                        textRole: "id"

                        // The dropdown is wider than the control and hangs off
                        // its right edge, leftward — the rows sit at the right
                        // edge of the window, so a centered popup would leave it.
                        popup: Popup {
                            y: quantBox.height + 2
                            width: 360
                            padding: 1
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

                            onAboutToShow: {
                                const p = quantBox.mapToItem(null, 0, 0)
                                const left = Math.max(Theme.spacingSmall, Math.min(p.x + quantBox.width - width, window.width - width - Theme.spacingSmall))
                                x = left - p.x
                            }

                            background: Rectangle {
                                color: Theme.surface
                                border.color: Theme.border
                                border.width: 1
                                radius: Theme.controlRadius
                            }

                            contentItem: Column {
                                spacing: 0

                                Repeater {
                                    model: quantBox.popup.visible && window.draft[taskRow.modelData]
                                           ? (taskRow.quantModel.rowFor(window.draft[taskRow.modelData].key).quants || [])
                                           : []

                                    delegate: MenuItem {
                                        id: quantEntry

                                        required property var modelData
                                        required property int index

                                        width: quantBox.popup.width - quantBox.popup.padding * 2
                                        height: Theme.controlHeight
                                        highlighted: quantEntry.index === quantBox.currentIndex
                                        text: window.quantItemText(quantEntry.modelData)
                                        onTriggered: {
                                            quantBox.choose(quantEntry.index)
                                            quantBox.popup.close()
                                        }
                                    }
                                }
                            }
                        }

                        displayText: {
                            const q = quantBox.model && quantBox.currentIndex >= 0 ? quantBox.model[quantBox.currentIndex] : null
                            return q ? q.id + "  " + window.quantMarker(q) : ""
                        }

                        function choose(index) {
                            // The custom popup bypasses ComboBox's own
                            // selection path, so currentIndex (the closed
                            // button's text follows it) is updated here.
                            currentIndex = index
                            if (window.draft[taskRow.modelData] && model[index])
                                window.draft[taskRow.modelData].quantId = model[index].id
                        }

                        function sync() {
                            const entry = window.draft[taskRow.modelData]
                            if (!entry)
                                return
                            const row = taskRow.quantModel.rowFor(entry.key)
                            model = row.quants || []
                            let idx = model.findIndex(q => q.id.toLowerCase() === String(entry.quantId).toLowerCase())
                            if (idx < 0)
                                idx = row.selected >= 0 && row.selected < model.length ? row.selected : 0
                            // A same-index re-assign emits nothing, and the
                            // display text would keep a stale install marker.
                            currentIndex = -1
                            currentIndex = idx
                            if (model[currentIndex])
                                entry.quantId = model[currentIndex].id
                        }

                        Component.onCompleted: quantBox.sync()
                        onActivated: function (index) {
                            quantBox.choose(index)
                        }
                    }

                    TextField {
                        id: nameField
                        visible: window.externalMode
                        Layout.preferredWidth: 210
                        implicitHeight: Theme.controlHeight
                        text: taskRow.entry.modelName || ""
                        onTextEdited: {
                            if (window.draft[taskRow.modelData])
                                window.draft[taskRow.modelData].modelName = nameField.text
                        }
                    }

                    ToolButton {
                        id: gearButton

                        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
                        implicitHeight: Math.max(Theme.controlHeight, topPadding + bottomPadding + contentItem.implicitHeight)
                        onClicked: window.openTaskSettings(taskRow.modelData, gearButton)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Launch and request settings")

                        contentItem: Item {
                            implicitWidth: gearIcon.size
                            implicitHeight: gearIcon.size

                            Icon {
                                id: gearIcon
                                anchors.centerIn: parent
                                name: "settings"
                                size: 15
                                color: gearButton.enabled ? Theme.textPrimary : Theme.textMuted
                            }
                        }
                    }
                }
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

    // Launch/Request settings of one task row, hidden behind its gear.
    Popup {
        id: taskSettingsPopup

        property string task: "pageOcr"

        function attachTo(button) {
            // The rows end at the window's right edge, so the popup hangs off
            // the gear's right edge leftward and is clamped into the window.
            const p = button.mapToItem(parent, 0, 0)
            x = Math.max(Theme.paddingWindow, Math.min(p.x + button.width - width, parent.width - width - Theme.paddingWindow))
            y = Math.max(Theme.paddingWindow, Math.min(p.y + button.height + Theme.spacingSmall, parent.height - height - Theme.paddingWindow))
        }

        parent: window.contentItem
        width: 560
        height: 480
        padding: Theme.spacing
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
            radius: Theme.controlRadius
        }

        onAboutToShow: {
            launchTab.loadValues()
            requestTab.loadValues()
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingSmall

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                LLOLabel {
                    Layout.fillWidth: true
                    font.bold: true
                    elide: Text.ElideRight
                    text: RoleProfiles.title(taskSettingsPopup.task)
                }
                ToolButton {
                    id: popupCloseButton

                    implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
                    implicitHeight: topPadding + bottomPadding + contentItem.implicitHeight
                    onClicked: taskSettingsPopup.close()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Close")

                    contentItem: Item {
                        implicitWidth: closeIcon.size
                        implicitHeight: closeIcon.size

                        Icon {
                            id: closeIcon
                            anchors.centerIn: parent
                            name: "x"
                            size: 15
                            color: Theme.textSecondary
                        }
                    }
                }
            }

            SettingsTabBar {
                id: popupTabs
                Layout.fillWidth: true
                titles: [qsTr("Launch"), qsTr("Request")]
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: popupTabs.currentIndex

                LaunchTab {
                    id: launchTab
                    role: taskSettingsPopup.task
                    runtimeSettingsRef: window.runtimeSettingsRef
                }

                RequestTab {
                    id: requestTab
                    role: taskSettingsPopup.task
                }
            }
        }
    }

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
