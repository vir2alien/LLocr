import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

GridLayout {
    columns: 2
    rowSpacing: 4
    columnSpacing: 8

    function loadValues() {
        temperatureField.text = Settings.temperature.toString()
        maxTokensField.text  = Settings.maxTokens.toString()
        dryMultiplierField.text = Settings.dryMultiplier.toString()
        dryBaseField.text = Settings.dryBase.toString()
        dryAllowedLengthField.text = Settings.dryAllowedLength.toString()
        dryRange.text = Settings.dryPenaltyLastN.toString()
    }

    function saveValues() {
        Settings.temperature = parseFloat(temperatureField.text) || 0.0;
        Settings.maxTokens = parseInt(maxTokensField.text) || 8192;
        Settings.dryMultiplier = parseFloat(dryMultiplierField.text) || 0.8;
        Settings.dryBase = parseFloat(dryBaseField.text) || 1.75;
        Settings.dryAllowedLength = parseInt(dryAllowedLengthField.text) || 35;
        Settings.dryPenaltyLastN = parseInt(dryRange.text) || 2048;
    }

    Item { Layout.columnSpan: 2; implicitHeight: 4 }

    LLOLabel {
        Layout.topMargin: 4
        text: qsTr("Temperature")
    }
    LLOLabel {
        Layout.topMargin: 4
        text: qsTr("Max tokens per page")
    }
    TextField {
        id: temperatureField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        validator: DoubleValidator { bottom: 0.0; top: 2.0; decimals: 2 }
    }
    TextField {
        id: maxTokensField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        inputMethodHints: Qt.ImhDigitsOnly
        validator: IntValidator { bottom: 1; top: 1000000 }
    }

    LLOLabel {
        Layout.topMargin: 4
        text: qsTr("DRY multiplier")
    }
    LLOLabel {
        Layout.topMargin: 4
        text: qsTr("DRY base")
    }
    TextField {
        id: dryMultiplierField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        validator: DoubleValidator { bottom: 0.0; top: 2.0; decimals: 2 }
    }
    TextField {
        id: dryBaseField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        validator: DoubleValidator { bottom: 0.0; top: 3.0; decimals: 2 }
    }

    LLOLabel {
        Layout.topMargin: 4
        text: qsTr("DRY allowed length")
    }
    LLOLabel {
        Layout.topMargin: 4
        text: qsTr("DRY range")
    }
    TextField {
        id: dryAllowedLengthField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        validator: IntValidator { bottom: 0;}
    }
    TextField {
        id: dryRange
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        validator: IntValidator { bottom: 0;}
    }

    Item {
        Layout.columnSpan: 2
        implicitHeight: 4
    }

    LLOLabel {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.textMuted
        text: qsTr("DRY (Don't Repeat Yourself) the parameters are selected for optimal recognition accuracy in llama.cpp")
    }

    Item { Layout.fillHeight: true }
}