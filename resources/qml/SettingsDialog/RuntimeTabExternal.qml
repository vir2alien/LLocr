import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ColumnLayout {
    spacing: 4

    function loadValues() {
        baseUrlField.text = Settings.baseUrl
        apiKeyField.text  = Settings.apiKey
        timeoutField.text = Settings.connectionTimeoutMs.toString()
    }

    function savaValues() {
        Settings.baseUrl = baseUrlField.text;
        Settings.apiKey = apiKeyField.text;
        Settings.connectionTimeoutMs = parseInt(timeoutField.text) || 120000;
    }

    Label {
        text: qsTr("Endpoint base URL")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
    }
    TextField {
        id: baseUrlField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        placeholderText: "http://localhost:8080"
        selectByMouse: true
    }

    Item { implicitHeight: 4 }

    Label {
        text: qsTr("API key (optional)")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        TextField {
            id: apiKeyField
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            selectByMouse: true
            echoMode: revealKey.checked ? TextInput.Normal
                                        : TextInput.Password
        }
        CheckBox {
            id: revealKey
            text: qsTr("Show")
            font.pixelSize: Theme.fontCaption
        }
    }

    Item { implicitHeight: 4 }

    Label {
        text: qsTr("Request timeout (ms)")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
    }
    TextField {
        id: timeoutField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        inputMethodHints: Qt.ImhDigitsOnly
        validator: IntValidator { bottom: 1000; top: 3600000 }
    }

    Item { implicitHeight: 6 }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSmall
        color: Theme.textMuted
        text: qsTr("Note: the API key is stored locally in plaintext. "
                   + "Avoid using production keys.")
    }
}
