pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// A key that lives in the OS secret store. The stored key is never shown
// back: the field carries a mask and takes a new one. The mask is the
// field's own text, so clearing the field removes the stored key the way
// clearing any text field does (ADR 227, ADR 283).
RowLayout {
    id: root

    // Whether a key is stored for this slot.
    property bool keySet: false
    // The typed text goes here — it is the new key. An empty text removes
    // the stored key.
    property var commit: function(text) { }
    // What an empty field means, shown as the placeholder.
    property string emptyHint: qsTr("the shared key")
    // Whether the typed text is shown instead of masked. The window owns
    // this state: one "Show" in the table header reveals every key field
    // of the tab at once.
    property bool revealed: false

    // The mask is text, so it can be erased like any other text.
    readonly property string maskText: "••••••••"

    // Whether the user touched the field. An untouched field keeps the
    // stored key; a cleared one removes it.
    property bool edited: false

    property alias text: keyField.text

    // The mask and the typed key must not mix: bullets alone are the mask,
    // and an erased mask is an empty field — which removes the stored key.
    function plainText(typed) {
        if (typed.length === 0)
            return ""
        if (/^•+$/.test(typed))
            return ""
        return typed.replace(/•/g, "")
    }

    function load() {
        keyField.text = root.keySet ? root.maskText : ""
        root.edited = false
    }

    function save() {
        if (root.edited)
            root.commit(plainText(keyField.text))
        keyField.text = root.keySet ? root.maskText : ""
        root.edited = false
    }

    TextField {
        id: keyField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        echoMode: root.revealed ? TextInput.Normal : TextInput.Password
        placeholderText: root.emptyHint
        onTextEdited: {
            root.edited = true
            keyField.text = root.plainText(keyField.text)
        }
    }
}
