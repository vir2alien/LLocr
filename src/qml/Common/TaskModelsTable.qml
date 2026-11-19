pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// The task rows shared by the Models window (ADR 168) and the setup
// wizard's hardware step (ADR 170): task | runtime ▾ | model ▾ | quant ▾ |
// name (external) | ⚙. The table edits a draft — task -> {key: profile id,
// quantId, modelName, runtimeMode}; the host applies it (Save in the window,
// Next in the wizard). The rows live in a layout; the popups stay outside it,
// so their fixed sizes are none of the layout's business.
Item {
    id: table

    implicitHeight: rowsLayout.implicitHeight

    // The user changed a pick in a row.
    signal edited()

    // The Launch tab of the gear popover asks for the runtime settings; the
    // host shows them (ADR 297).
    signal runtimeSectionRequested()

    property bool showGear: true

    // The host that shows no runtime of its own (the setup wizard) shows the
    // local models of every task: the runtime of a task is chosen in the
    // settings, and the narrow window of the wizard has no room for the
    // column.
    property bool showRuntimeColumn: true

    // The host can show the runtime settings; the setup wizard cannot, and
    // the button that asks for them stays hidden there.
    property bool runtimeSectionAvailable: true

    // The tasks of the app: the list comes from the C++, where ConnectionRole
    // is its single source (ADR 215) — the table keeps no copy of it.
    readonly property var taskIds: Settings.taskRoles

    // The columns of the table. The runtime, the quantization and the gear
    // have the width their content needs, the model name takes the room that
    // is left and no more, and the free space goes to the right of the gear —
    // the model name must not push the quantization out of sight, and the
    // gear must stand next to the fields it belongs to (ADR 317).
    readonly property int taskColumnWidth: 140
    readonly property int runtimeColumnWidth: 200
    readonly property int quantColumnWidth: 150
    readonly property int nameColumnWidth: 150
    readonly property int gearColumnWidth: 36
    // The row without the model column: the task, the runtime, the
    // quantization (or the alias of an external task), the gear and the gaps
    // between them.
    readonly property int fixedColumnWidth: taskColumnWidth + runtimeColumnWidth
                                             + quantColumnWidth + gearColumnWidth
                                             + Theme.spacingSmall * 5
    readonly property int modelColumnMaxWidth: 260
    readonly property int modelColumnWidth: Math.max(140, Math.min(modelColumnMaxWidth,
                                                                     width - fixedColumnWidth))

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
            // The runtime of the task: managed (the local llama-server) or
            // external (its own endpoint).
            property string runtimeMode: "managed"
            // The user touched the field: a re-sync refreshes the untouched
            // fields only, so an unsaved edit is not thrown away.
            property bool keyEdited: false
            property bool quantEdited: false
            property bool nameEdited: false
            property bool runtimeModeEdited: false
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

    // The runtime of a task: the draft's own choice, else the saved one.
    function runtimeModeForTask(task) {
        const entry = table.draftEntry(task)
        if (entry && entry.runtimeMode.length > 0)
            return entry.runtimeMode
        return table.savedRuntimeMode(task)
    }

    // The runtime a task is saved with: the host that shows no runtime of
    // its own picks the local models of every task.
    function savedRuntimeMode(task) {
        return table.showRuntimeColumn ? Settings.runtimeModeForRole(task) : "managed"
    }

    // The runtime of a task is written at once: the gear popup reads it to
    // show the tabs of that task, and the settings keep the choice.
    function setRuntimeModeForTask(task, mode) {
        Settings.setRuntimeModeForRole(task, mode)
        const entry = table.draftEntry(task)
        if (entry) {
            entry.runtimeMode = mode
            entry.runtimeModeEdited = true
        }
    }

    // Draft quant id for a task's profile: the row's own default — the active
    // quant, else the saved choice, else the first installed one (the same
    // order the models list uses). Empty for an external task, whose model
    // lives on the other server, and when the profile has no quantization
    // rows.
    function draftQuantId(task, key) {
        const entry = table.draftEntry(task)
        if (entry && entry.runtimeMode === "external")
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
            if (!existing)
                entry.runtimeMode = table.savedRuntimeMode(task)
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
            if (!entry.runtimeModeEdited)
                entry.runtimeMode = table.savedRuntimeMode(task)
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
        connectionTab.resetValues()
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (!entry)
                continue
            entry.keyEdited = false
            entry.quantEdited = false
            entry.nameEdited = false
            entry.runtimeModeEdited = false
            entry.runtimeMode = table.savedRuntimeMode(task)
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
            entry.runtimeModeEdited = false
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
    // An external task has no local model: its server carries it.
    function buildChoices() {
        var choices = []
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (entry && entry.quantId.length > 0 && entry.runtimeMode !== "external")
                choices.push({task: task, key: entry.key, quantId: entry.quantId})
        }
        return choices
    }

    // The runtime of every task is written, and the request profile of every
    // task with it: the profile is the shape of the request, not of the
    // runtime.
    function applyRuntimeModes() {
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (!entry)
                continue
            Settings.setRuntimeModeForRole(task, entry.runtimeMode)
            RoleProfiles.setRequestProfileId(task, entry.key)
        }
        Settings.forceSave()
    }

    // All three tabs are written, and the answer tells the host whether they
    // were: a tab that asks the user a question first has written nothing.
    function saveTabs() {
        const launchSaved = launchTab.saveValues()
        const requestSaved = requestTab.saveValues()
        connectionTab.saveValues()
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (entry)
                entry.modelName = Settings.modelNameForRole(task)
        }
        return launchSaved && requestSaved
    }

    function resetTabs() {
        launchTab.resetValues()
        requestTab.resetValues()
        connectionTab.resetValues()
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
        if (q.cpuOffload === true && q.ramLabel.length > 0)
            s += " · " + qsTr("RAM ≥ %1 · VRAM ≥ %2 (split) / %3 (full)").arg(q.ramLabel).arg(q.vramFloorLabel).arg(q.vramLabel)
        else if (q.vramLabel.length > 0)
            s += " · " + qsTr("VRAM ≥ %1").arg(q.vramLabel)
        return s
    }

    // The width a text needs in the font of the menu entries. The popup of
    // a quantization is as wide as its widest entry — the numbers behind a
    // quantization are the reason to open it, and a clipped line says
    // nothing.
    function textWidth(text) {
        return quantMetrics.advanceWidth(text)
    }

    // The width the dropdown of a quantization needs: the widest entry plus
    // the padding of a menu item. The control's own width is the lower
    // bound — a short list still opens a dropdown of the field's size.
    function quantPopupWidth(quants, minimumWidth) {
        let widest = 0
        for (let i = 0; i < (quants || []).length; ++i)
            widest = Math.max(widest, textWidth(quantItemText(quants[i])))
        return Math.max(minimumWidth, widest + Theme.spacing * 4)
    }

    FontMetrics {
        id: quantMetrics
        font: Theme.button
    }

    // The entries exist before the rows are built: the rows are the entries.
    Component.onCompleted: {
        table.ensureDraft()
    }

    ColumnLayout {
        id: rowsLayout

        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Item { Layout.preferredWidth: table.taskColumnWidth }
            LLOLabel {
                Layout.preferredWidth: table.runtimeColumnWidth
                visible: table.showRuntimeColumn
                font.pixelSize: Theme.footnoteSize
                color: Theme.textMuted
                text: qsTr("Runtime")
            }
            LLOLabel {
                Layout.preferredWidth: table.modelColumnWidth
                font.pixelSize: Theme.footnoteSize
                color: Theme.textMuted
                text: qsTr("Model")
            }
            Item { Layout.fillWidth: true }
        }

        Repeater {
            model: table.draft

            delegate: RowLayout {
                id: taskRow

                required property var modelData

                readonly property string task: taskRow.modelData.task
                readonly property var quantModel: RoleProfiles.quantModels(taskRow.task)
                readonly property var entry: taskRow.modelData
                // The runtime of this task: an external task has no local
                // model and no quantization of its own.
                readonly property bool external: taskRow.entry.runtimeMode === "external"

                // The row fills the width of the table and may shrink below
                // its own width: the narrow window of the wizard has no room
                // for every column at once.
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: Theme.spacingSmall

                Item {
                    Layout.preferredWidth: table.taskColumnWidth
                    implicitHeight: modelBox.implicitHeight

                    LLOLabel {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSmall
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        font.pixelSize: Theme.bodySmallSize
                        text: RoleProfiles.title(taskRow.task)
                        color: Theme.textPrimary
                    }
                }

                SegmentedControl {
                    Layout.preferredWidth: table.runtimeColumnWidth
                    Layout.minimumWidth: table.runtimeColumnWidth
                    visible: table.showRuntimeColumn
                    options: [
                        { label: qsTr("Managed") },
                        { label: qsTr("External") }
                    ]
                    currentIndex: taskRow.external ? 1 : 0
                    onActivated: (index) => {
                        taskRow.entry.runtimeMode = index === 1 ? "external" : "managed"
                        taskRow.entry.runtimeModeEdited = true
                        table.edited()
                    }
                }

                LLOComboBox {
                    id: modelBox
                    Layout.preferredWidth: table.modelColumnWidth
                    Layout.minimumWidth: 140
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
                        table.edited()
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

                LLOComboBox {
                    id: quantBox
                    visible: !taskRow.external
                    Layout.preferredWidth: table.quantColumnWidth
                    Layout.minimumWidth: table.quantColumnWidth
                    textRole: "id"

                    // The dropdown is wider than the control and hangs off
                    // its right edge, leftward — the rows sit at the right
                    // edge of the host, so a centered popup would leave it.
                    popup: Popup {
                        id: quantPopup
                        y: quantBox.height + 2
                        padding: 1
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

                        onAboutToShow: {
                            const host = Overlay.overlay
                            const p = quantBox.mapToItem(null, 0, 0)
                            // The popup is as wide as its widest entry: the
                            // numbers behind a quantization are the reason to
                            // open it, and a clipped line says nothing. It
                            // never grows past the window that holds it.
                            width = Math.min(table.quantPopupWidth(quantBox.model, quantBox.width),
                                             host.width - Theme.spacing * 2)
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

                                    font: Theme.button
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
                        table.edited()
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
                    visible: taskRow.external
                    Layout.preferredWidth: table.nameColumnWidth
                    implicitHeight: Theme.controlHeight
                    font.pixelSize: Theme.bodySmallSize
                    text: taskRow.entry.modelName || ""
                    // The alias belongs to the connection: it is edited in
                    // the Connection tab of the gear popup, where the
                    // endpoint and the key live (ADR 226). Here it is shown.
                    readOnly: true
                    selectByMouse: true
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
                            size: Theme.iconSizeMedium
                            color: gearButton.enabled ? Theme.textPrimary : Theme.textMuted
                        }
                    }
                }

                // The free space of the row lies to the right of the gear:
                // the fields and the button that opens their settings stand
                // together, and the empty place is at the edge.
                Item { Layout.fillWidth: true }
            }
        }
    }

    // The settings of one task row, hidden behind its gear: the launch and
    // the request of a managed task, the connection and the request of an
    // external one.
    Popup {
        id: taskSettingsPopup

        property string task: "blockRecognition"

        // The runtime of the task: it decides which tabs the popup shows.
        property string taskMode: "managed"

        // The overlay, not the table: in the wizard the rows live inside a
        // dialog, and a centered popup fits either host.
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 620
        // The popup follows its content: the taller of the tabs sets the
        // height, and the notes stay under the table.
        height: Math.max(340, Math.min(500,
                                       taskSettingsPopup.currentContentHeight
                                       + popupTitle.implicitHeight + popupTabs.implicitHeight
                                       + Theme.spacing * 4))
        padding: Theme.spacing
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // The page the tab bar points at: the external task shows the
        // connection where the managed one shows the launch.
        readonly property real currentContentHeight: {
            const index = taskSettingsPopup.taskMode === "external"
                          ? (popupTabs.currentIndex === 0 ? 2 : 1)
                          : popupTabs.currentIndex
            return index === 0 ? launchTab.contentHeight
                 : index === 1 ? requestTab.contentHeight
                 : connectionTab.contentHeight
        }

        function rebuildTabs() {
            taskSettingsPopup.taskMode = table.runtimeModeForTask(taskSettingsPopup.task)
        }

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
            radius: Theme.controlRadius
        }

        onAboutToShow: {
            taskSettingsPopup.rebuildTabs()
            launchTab.loadValues()
            requestTab.loadValues()
            connectionTab.loadValues()
        }

        contentItem: ColumnLayout {
            id: popupContent
            spacing: Theme.spacingSmall

            RowLayout {
                id: popupTitle
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                LLOLabel {
                    Layout.fillWidth: true
                    font.pixelSize: Theme.bodySize
                    font.bold: true
                    color: Theme.textPrimary
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
                            size: Theme.iconSizeMedium
                            color: Theme.textSecondary
                        }
                    }
                }
            }

            SettingsTabBar {
                id: popupTabs
                Layout.fillWidth: true
                titles: taskSettingsPopup.taskMode === "external"
                         ? [qsTr("Connection"), qsTr("Request")]
                         : [qsTr("Launch"), qsTr("Request")]
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: taskSettingsPopup.taskMode === "external"
                              ? (popupTabs.currentIndex === 0 ? 2 : 1)
                              : popupTabs.currentIndex

                LaunchTab {
                    id: launchTab
                    role: taskSettingsPopup.task
                    hostTable: table
                    runtimeSectionAvailable: table.runtimeSectionAvailable
                    onEdited: table.edited()
                }

                RequestTab {
                    id: requestTab
                    role: taskSettingsPopup.task
                    onEdited: table.edited()
                }

                TaskConnectionTab {
                    id: connectionTab
                    task: taskSettingsPopup.task
                    onEdited: table.edited()
                    onCloseRequested: taskSettingsPopup.close()
                }
            }
        }
    }
}
