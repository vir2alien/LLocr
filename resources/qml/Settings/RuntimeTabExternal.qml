pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ColumnLayout {
    spacing: 4

    function loadValues() {
        ocrModelNameField.text = Settings.modelName
        checkModelNameField.text = Settings.checkModelName
        baseUrlField.text = Settings.baseUrl
        apiKeyField.text  = Settings.apiKey
        timeoutField.text = Settings.connectionTimeoutMs.toString()
    }

    function saveValues() {
        Settings.modelName = ocrModelNameField.text;
        Settings.checkModelName = checkModelNameField.text;
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

    LLOLabel {
        text: qsTr("Model name (OCR)")
    }
    TextField {
        id: ocrModelNameField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        placeholderText: qsTr("e.g. Unlimited-OCR, or the id your server exposes")
    }

    LLOLabel {
        text: qsTr("Model name (validator)")
    }
    TextField {
        id: checkModelNameField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        placeholderText: qsTr("e.g. qwen3.5-4b, or the id your server exposes")
    }
    LLOLabel {
        font.pointSize: Theme.captionSize
        color: Theme.helpColor
        text: qsTr("Optional model alias; can be left empty for a single-model server")
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
        LLOCheckBox {
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
        color: Theme.helpColor
        text: qsTr("Note: the API key is stored locally in plaintext. "
                   + "Avoid using production keys.")
    }
}
