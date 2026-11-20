pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// The endpoint and the key of one task: what the gear of the task row opens.
// The drafts live in the row — the form shows them and hands the changes
// back, so a closed popup does not lose what the user typed.
ColumnLayout {
    id: form

    property string taskTitle: ""
    property string endpointDraft: ""
    property string keyDraft: ""
    property bool keyEdited: false
    property bool keyPresent: false
    property bool revealed: false
    // The popup's own switch: the tab's switch reveals every key, this one
    // reveals this key alone.
    property bool localReveal: false

    signal edited(string endpoint, string key, bool keyEdited)

    spacing: Theme.spacingSmall

    function syncFields() {
        endpointField.text = form.endpointDraft
        keyCell.text = form.keyEdited ? form.keyDraft
                                      : (form.keyPresent ? keyCell.maskText : "")
        keyCell.edited = form.keyEdited
    }

    Component.onCompleted: form.syncFields()

    onEndpointDraftChanged: {
        if (endpointField.text !== form.endpointDraft)
            endpointField.text = form.endpointDraft
    }
    onKeyDraftChanged: form.syncFields()
    onKeyEditedChanged: form.syncFields()
    onKeyPresentChanged: form.syncFields()

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        LLOLabel {
            Layout.fillWidth: true
            text: form.taskTitle
            font.pixelSize: Theme.bodySize
            font.bold: true
            color: Theme.textPrimary
            elide: Text.ElideRight
        }
    }

    LLOLabel {
        text: qsTr("Endpoint")
        font.pixelSize: Theme.bodySmallSize
        color: Theme.textPrimary
    }

    TextField {
        id: endpointField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        font.pixelSize: Theme.bodySmallSize
        selectByMouse: true
        placeholderText: Settings.baseUrl
        onTextEdited: {
            // The draft follows the field: a stale copy would come back when
            // the key is typed and would take the typed endpoint with it.
            form.endpointDraft = text
            form.edited(text, form.keyDraft, form.keyEdited)
        }
    }

    LLOLabel {
        Layout.fillWidth: true
        visible: Settings.isPlaintextKeyEndpoint(endpointField.text)
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.footnoteSize
        color: Theme.accentAttention
        text: qsTr("The key will be sent in plain text (http outside the local machine).")
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        LLOLabel {
            text: qsTr("API key")
            font.pixelSize: Theme.bodySmallSize
            color: Theme.textPrimary
        }
        LLOCheckBox {
            text: qsTr("Show")
            font.pixelSize: Theme.footnoteSize
            checked: form.localReveal
            onToggled: form.localReveal = checked
        }
        Item { Layout.fillWidth: true }
    }

    SecretKeyField {
        id: keyCell
        Layout.fillWidth: true
        revealed: form.localReveal || form.revealed
        emptyHint: qsTr("the shared key")
        onTyped: (plain) => {
            form.keyDraft = plain
            form.keyEdited = true
            form.edited(form.endpointDraft, plain, true)
        }
    }
}
