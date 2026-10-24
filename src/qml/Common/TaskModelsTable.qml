pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// The six task rows shared by the Models window (ADR 168) and the setup
// wizard's hardware step (ADR 170): task | model ▾ | quant ▾ | name
// (external mode) | ⚙. The table edits a draft — task -> {key: profile id,
// quantId, modelName}; the host applies it (Save in the window, Next in the
// wizard). The rows live in a layout; the popups stay outside it, so their
// fixed sizes are none of the layout's business.
Item {
    id: table

    implicitHeight: rowsLayout.implicitHeight

    // The Launch tab of the gear popover opens the runtime settings window;
    // null when the host does not provide it (the wizard).
    property var runtimeSettingsRef: null
    property bool externalMode: false
    property bool showGear: true

    readonly property var taskIds: ["pageOcr", "rawPageOcr", "blockRecognition", "decision", "layout", "fix"]

    // The draft: one entry per task. Every entry is a real object with
    // properties, so a change is a change of the entry and the row binds to
    // the entry itself — not to a snapshot of a plain object (ADR 200).
    property list<QtObject> draft: []

    Component {
        id: draftEntryComponent
        QtObject {
            property string task: ""
            property string key: ""
            property string quantId: ""
            property string modelName: ""
            // The user touched the field: a re-sync refreshes the untouched
            // fields only, so an unsaved edit is not thrown away.
            property bool keyEdited: false
            property bool quantEdited: false
            property bool nameEdited: false
        }
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
        if (table.externalMode)
            return ""
        const row = RoleProfiles.quantModels(task).rowFor(key)
        const quants = row.quants || []
        const idx = row.selected >= 0 && row.selected < quants.length ? row.selected : 0
        return quants[idx] ? quants[idx].id : ""
    }

    function draftEntry(task) {
        for (let i = 0; i < table.draft.length; ++i) {
            if (table.draft[i].task === task)
                return table.draft[i]
        }
        return null
    }

    function ensureDraft() {
        let missing = false
        for (const task of table.taskIds) {
            if (!table.draftEntry(task))
                missing = true
        }
        if (!missing)
            return
        const list = []
        for (const task of table.taskIds) {
            const existing = table.draftEntry(task)
            const entry = existing ? existing : draftEntryComponent.createObject(table)
            entry.task = task
            list.push(entry)
        }
        table.draft = list
    }

    // Refreshes the draft from the settings without touching what the user
    // has changed since: the window is shown again, the wizard step is
    // entered again, and the picks of that moment must survive it.
    function syncDraft() {
        table.ensureDraft()
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (!entry)
                continue
            if (!entry.keyEdited)
                entry.key = RoleProfiles.requestProfileId(task)
            if (!entry.quantEdited)
                entry.quantId = table.draftQuantId(task, entry.key)
            if (!entry.nameEdited)
                entry.modelName = Settings.modelNameForRole(task)
        }
    }

    // Throws the unsaved edits away and takes the saved values (the Cancel
    // button of the models window).
    function resetDraft() {
        table.ensureDraft()
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (!entry)
                continue
            entry.keyEdited = false
            entry.quantEdited = false
            entry.nameEdited = false
            entry.key = RoleProfiles.requestProfileId(task)
            entry.quantId = table.draftQuantId(task, entry.key)
            entry.modelName = Settings.modelNameForRole(task)
        }
    }

    // The picks are in the settings now: the next sync may refresh them from
    // there.
    function markDraftSaved() {
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (!entry)
                continue
            entry.keyEdited = false
            entry.quantEdited = false
            entry.nameEdited = false
        }
    }

    // The wizard's auto-selection lands here: entries for tasks whose choice
    // is being introduced for the first time.
    function prefillChoices(choices) {
        for (const choice of choices) {
            const entry = table.draftEntry(choice.task)
            if (!entry)
                continue
            entry.key = choice.key
            entry.quantId = choice.quantId
        }
    }

    // The host's apply flow: choices feeds ModelInstaller.applyTaskChoices.
    function buildChoices() {
        var choices = []
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (entry && entry.quantId.length > 0)
                choices.push({task: task, key: entry.key, quantId: entry.quantId})
        }
        return choices
    }

    // The external mode persists the profile and model-name picks only.
    function applyExternalDraft() {
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (!entry)
                continue
            RoleProfiles.setRequestProfileId(task, entry.key)
            Settings.setModelNameForRole(task, entry.modelName)
        }
        Settings.forceSave()
        table.markDraftSaved()
    }

    function saveTabs() {
        launchTab.saveValues()
        requestTab.saveValues()
    }

    function resetTabs() {
        launchTab.resetValues()
        requestTab.resetValues()
    }

    function openTaskSettings(task) {
        taskSettingsPopup.task = task
        taskSettingsPopup.open()
    }

    function quantMarker(q) {
        return q.installed ? "✓" : (q.downloadable ? "⬇" : "·")
    }

    function quantItemText(q) {
        let s = table.quantMarker(q) + " " + q.id
        if (q.sizeLabel.length > 0)
            s += " — " + q.sizeLabel
        if (q.vramLabel.length > 0)
            s += " · " + qsTr("VRAM ≥ %1").arg(q.vramLabel)
        return s
    }

    // The entries exist before the rows are built: the rows are the entries.
    Component.onCompleted: table.ensureDraft()

    ColumnLayout {
        id: rowsLayout

        anchors.left: parent.left
        anchors.right: parent.right
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
            Item { Layout.preferredWidth: 105 }
            Item { Layout.preferredWidth: 36 }
        }

        Repeater {
            model: table.draft

            delegate: RowLayout {
                id: taskRow

                required property var modelData

                readonly property string task: taskRow.modelData.task
                readonly property var quantModel: RoleProfiles.quantModels(taskRow.task)
                readonly property var entry: taskRow.modelData

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
                        text: RoleProfiles.title(taskRow.task)
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
                        const entry = taskRow.entry
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
                        taskRow.entry.key = modelBox.valueAt(index)
                        taskRow.entry.keyEdited = true
                        // The saved quant choice belongs to the previous
                        // profile; the new row picks its own default.
                        taskRow.entry.quantId = ""
                        taskRow.entry.quantEdited = false
                        quantBox.sync()
                    }
                    Connections {
                        target: taskRow.entry
                        function onKeyChanged() { modelBox.sync() }
                    }
                    Connections {
                        target: ModelInstaller
                        function onInstalledChanged() { modelBox.sync() }
                        function onPresetsChanged() { modelBox.sync() }
                    }
                }

                ComboBox {
                    id: quantBox
                    visible: !table.externalMode
                    Layout.preferredWidth: 105
                    implicitHeight: Theme.controlHeight
                    textRole: "id"

                    // The dropdown is wider than the control and hangs off
                    // its right edge, leftward — the rows sit at the right
                    // edge of the host, so a centered popup would leave it.
                    popup: Popup {
                        y: quantBox.height + 2
                        width: 360
                        padding: 1
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

                        onAboutToShow: {
                            const host = Overlay.overlay
                            const p = quantBox.mapToItem(null, 0, 0)
                            const left = Math.max(Theme.spacingSmall,
                                                  Math.min(p.x + quantBox.width - width,
                                                           host.width - width - Theme.spacingSmall))
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
                                model: quantBox.popup.visible && taskRow.entry
                                       ? (taskRow.quantModel.rowFor(taskRow.entry.key).quants || [])
                                       : []

                                delegate: MenuItem {
                                    id: quantEntry

                                    required property var modelData
                                    required property int index

                                    width: quantBox.popup.width - quantBox.popup.padding * 2
                                    height: Theme.controlHeight
                                    highlighted: quantEntry.index === quantBox.currentIndex
                                    text: table.quantItemText(quantEntry.modelData)
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
                        return q ? q.id + "  " + table.quantMarker(q) : ""
                    }

                    function choose(index) {
                        // The custom popup bypasses ComboBox's own
                        // selection path, so currentIndex (the closed
                        // button's text follows it) is updated here.
                        currentIndex = index
                        if (taskRow.entry && model[index]) {
                            taskRow.entry.quantId = model[index].id
                            taskRow.entry.quantEdited = true
                        }
                    }

                    function sync() {
                        const entry = taskRow.entry
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
                    visible: table.externalMode
                    Layout.preferredWidth: 210
                    implicitHeight: Theme.controlHeight
                    text: taskRow.entry.modelName || ""
                    onTextEdited: {
                        taskRow.entry.modelName = nameField.text
                        taskRow.entry.nameEdited = true
                    }
                }

                ToolButton {
                    id: gearButton

                    visible: table.showGear

                    implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
                    implicitHeight: Math.max(Theme.controlHeight, topPadding + bottomPadding + contentItem.implicitHeight)
                    onClicked: table.openTaskSettings(taskRow.task)
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

    // Launch/Request settings of one task row, hidden behind its gear.
    Popup {
        id: taskSettingsPopup

        property string task: "pageOcr"

        // The overlay, not the table: in the wizard the rows live inside a
        // dialog, and a centered popup fits either host.
        parent: Overlay.overlay
        anchors.centerIn: parent
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
                    runtimeSettingsRef: table.runtimeSettingsRef
                }

                RequestTab {
                    id: requestTab
                    role: taskSettingsPopup.task
                }
            }
        }
    }
}
