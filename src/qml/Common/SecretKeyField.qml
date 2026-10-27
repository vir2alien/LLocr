pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// A key that lives in the OS secret store. The stored key is never shown
// back: the field carries a mask and takes a new one. "Delete" removes the
// stored key and appears as a disabled button when there is nothing to
// remove (ADR 227).
RowLayout {
    id: root

    // Whether a key is stored for this slot.
    property bool keySet: false
    // The typed text goes here — it is the new key.
    property var commit: function(text) { }
    // The stored key goes away here.
    property var clear: function() { }
    // What an empty field means, shown as the placeholder.
    property string emptyHint: qsTr("the shared key")

    function load() {
        keyField.text = ""
        reveal.checked = false
    }

    function save() {
        if (keyField.text.length > 0)
            root.commit(keyField.text)
        keyField.text = ""
        reveal.checked = false
    }

    TextField {
        id: keyField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        echoMode: reveal.checked ? TextInput.Normal : TextInput.Password
        placeholderText: root.keySet ? "••••••••" : root.emptyHint
    }

    LLOCheckBox {
        id: reveal
        text: qsTr("Show")
        font.pointSize: Theme.captionSize
    }

    LLOButton {
        enabled: root.keySet
        subtle: true
        text: qsTr("Delete")
        implicitHeight: Theme.controlHeight
        onClicked: root.clear()
    }
}
