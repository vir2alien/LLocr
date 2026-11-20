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

    // The user changed a value of the connection.
    signal edited()
    // The form's own close button: the tab has no popup of its own, the
    // popup of the task closes.
    signal closeRequested()

    spacing: Theme.spacingSmall

    // The height of the tab's content: the popup that holds the tabs sizes
    // itself by the taller of them.
    readonly property real contentHeight: root.implicitHeight

    property string aliasDraft: ""
    property string endpointDraft: ""
    property string keyDraft: ""
    property bool keyEdited: false
    property bool keyPresent: false

    function loadValues() {
        root.aliasDraft = Settings.modelNameForRole(root.task)
        root.endpointDraft = Settings.baseUrlForRole(root.task)
        root.keyPresent = Settings.apiKeySetForRole(root.task)
        root.keyDraft = ""
        root.keyEdited = false
        aliasField.text = root.aliasDraft
        // The form keeps its own copy of the drafts: a form that is bound to
        // them would keep the text of the key the user typed when the
        // window is cancelled.
        endpointForm.endpointDraft = root.endpointDraft
        endpointForm.keyDraft = ""
        endpointForm.keyEdited = false
        endpointForm.keyPresent = root.keyPresent
        // The fields follow the drafts even when the drafts did not change:
        // the user typed an endpoint, the load puts the saved one back.
        endpointForm.syncFields()
    }

    function saveValues() {
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
            root.edited()
        }
        onCloseRequested: root.closeRequested()
    }
}
