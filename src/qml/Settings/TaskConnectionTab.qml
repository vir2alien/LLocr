pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// The connection of one task: its endpoint, its key and its model alias.
// An empty endpoint or an empty key means the shared value.
ColumnLayout {
    id: root

    property string task: ""

    // The table that holds the draft of the tasks. The popup is a view of the
    // draft of its task: what the user types belongs to the window, so a
    // closed popup does not lose it, another task's popup does not take it
    // away, and the window's Cancel throws it away (ADR 297).
    property var hostTable: null

    // The user changed a value of the connection.
    signal edited()

    spacing: Theme.spacingSmall

    // The height of the tab's content: the popup that holds the tabs sizes
    // itself by the taller of them.
    readonly property real contentHeight: root.implicitHeight

    // The draft of the task: the entry of the task in the table's draft when
    // the tab stands in a table, nothing when it stands on its own. The
    // draft is read on purpose: the entries appear when the table builds
    // them, and the binding must follow.
    readonly property var entry: {
        if (!root.hostTable || root.hostTable.draft.length === 0)
            return null
        return root.hostTable.draftEntry(root.task)
    }

    property string aliasDraft: ""
    property string endpointDraft: ""
    property string keyDraft: ""
    property bool keyEdited: false
    property bool keyPresent: false

    function loadValues() {
        const entry = root.entry
        if (entry) {
            root.aliasDraft = entry.modelName
            root.endpointDraft = entry.endpoint
            root.keyPresent = Settings.apiKeySetForRole(root.task)
            root.keyDraft = entry.apiKeyEdited ? entry.apiKey : ""
            root.keyEdited = entry.apiKeyEdited
        } else {
            root.aliasDraft = Settings.modelNameForRole(root.task)
            root.endpointDraft = Settings.baseUrlForRole(root.task)
            root.keyPresent = Settings.apiKeySetForRole(root.task)
            root.keyDraft = ""
            root.keyEdited = false
        }
        aliasField.text = root.aliasDraft
        // The form keeps its own copy of the drafts: a form that is bound to
        // them would keep the text of the key the user typed when the
        // window is cancelled.
        endpointForm.endpointDraft = root.endpointDraft
        endpointForm.keyDraft = root.keyDraft
        endpointForm.keyEdited = root.keyEdited
        endpointForm.keyPresent = root.keyPresent
        // The fields follow the drafts even when the drafts did not change:
        // the user typed an endpoint, the load puts the saved one back.
        endpointForm.syncFields()
    }

    // The drafts go back to the table's draft: the window writes them when
    // it is saved.
    function storeDraft() {
        const entry = root.entry
        if (!entry)
            return
        if (entry.modelName !== root.aliasDraft) {
            entry.modelName = root.aliasDraft
            entry.nameEdited = true
        }
        if (entry.endpoint !== root.endpointDraft) {
            entry.endpoint = root.endpointDraft
            entry.endpointEdited = true
        }
        if (root.keyEdited) {
            entry.apiKey = root.keyDraft
            entry.apiKeyEdited = true
        }
    }

    function saveValues() {
        // In a table the window writes the drafts of every task: the tab
        // hands its own over.
        if (root.entry) {
            root.storeDraft()
            return
        }
        Settings.setModelNameForRole(root.task, root.aliasDraft)
        Settings.setBaseUrlForRole(root.task, root.endpointDraft)
        if (root.keyEdited)
            Settings.setApiKeyForRole(root.task, root.keyDraft)
    }

    function resetValues() {
        root.loadValues()
    }

    onTaskChanged: root.loadValues()

    Component.onCompleted: root.loadValues()

    LLOLabel {
        text: qsTr("Model alias")
        font.pixelSize: Theme.bodySmallSize
        color: Theme.textPrimary
    }

    TextField {
        id: aliasField

        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        font.pixelSize: Theme.bodySmallSize
        selectByMouse: true
        placeholderText: qsTr("the model id")
        onTextEdited: {
            root.aliasDraft = aliasField.text
            root.storeDraft()
            root.edited()
        }
    }

    LLOLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.footnoteSize
        color: Theme.helpColor
        text: qsTr("An empty endpoint and an empty key take the shared server from Settings → Runtime.")
    }

    TaskEndpointForm {
        id: endpointForm

        Layout.fillWidth: true
        taskTitle: qsTr("Endpoint and key")
        onEdited: (endpoint, key, keyIsEdited) => {
            root.endpointDraft = endpoint
            root.keyDraft = key
            root.keyEdited = keyIsEdited
            root.storeDraft()
            root.edited()
        }
    }
}
