import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

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

    LLOLabel {
        text: qsTr("Endpoint base URL")
    }
    TextField {
        id: baseUrlField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        placeholderText: "http://localhost:8080"
        selectByMouse: true
    }

    Item { implicitHeight: 4 }

    LLOLabel {
        text: qsTr("API key (optional)")
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
            font.pointSize: Theme.captionSize
        }
    }

    Item { implicitHeight: 4 }

    LLOLabel {
        text: qsTr("Request timeout (ms)")
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

    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.textMuted
        text: qsTr("Note: the API key is stored locally in plaintext. "
                   + "Avoid using production keys.")
    }
}
