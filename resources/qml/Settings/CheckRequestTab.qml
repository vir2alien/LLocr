pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// Request parameters for the verification (check) model. Applied on Save
// (unlike the interface settings, which apply immediately).
ColumnLayout {
    id: root

    spacing: 4

    function loadValues() {
        temperatureField.text = Settings.checkTemperature.toString()
        maxTokensField.text = Settings.checkMaxTokens.toString()
        streamCheck.checked = Settings.checkStream
    }

    function saveValues() {
        var t = parseFloat(temperatureField.text)
        if (isNaN(t) || t < 0 || t > 2)
            t = 0.0
        Settings.checkTemperature = t

        var n = parseInt(maxTokensField.text)
        if (isNaN(n) || n < 1)
            n = 2048
        Settings.checkMaxTokens = n

        Settings.checkStream = streamCheck.checked
    }

    function resetValues() {
        // Keep in sync with SettingsStore::kDefaultCheck*.
        Settings.checkTemperature = 0.0
        Settings.checkMaxTokens = 2048
        Settings.checkStream = false
        root.loadValues()
    }

    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.textMuted
        text: qsTr("Parameters of the request sent to the verification model. "
                   + "Applied on Save.")
    }

    Item { implicitHeight: 6 }

    LLOLabel {
        text: qsTr("Temperature")
    }
    TextField {
        id: temperatureField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        validator: DoubleValidator { bottom: 0; top: 2; decimals: 3 }
    }
    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.textMuted
        text: qsTr("0 = deterministic greedy decoding — recommended for "
                   + "verification.")
    }

    Item { implicitHeight: 6 }

    LLOLabel {
        text: qsTr("Max tokens")
    }
    TextField {
        id: maxTokensField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        inputMethodHints: Qt.ImhDigitsOnly
        validator: IntValidator { bottom: 1; top: 32768 }
    }
    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.textMuted
        text: qsTr("Upper bound on the generated answer length.")
    }

    Item { implicitHeight: 6 }

    CheckBox {
        id: streamCheck
        font.pointSize: Theme.captionSize
        text: qsTr("Stream tokens (ignored — the check expects one complete "
                   + "response)")
    }

    Item { Layout.fillHeight: true }
}