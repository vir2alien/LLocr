import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

GridLayout {
    columns: 2
    rowSpacing: 4
    columnSpacing: 8

    function loadValues() {
        modelNameField.text  = Settings.modelName
        temperatureField.text = Settings.temperature.toString()
        maxTokensField.text  = Settings.maxTokens.toString()
        dryMultiplierField.text = Settings.dryMultiplier.toString()
        dryBaseField.text = Settings.dryBase.toString()
        dryAllowedLengthField.text = Settings.dryAllowedLength.toString()
        dryRange.text = Settings.dryPenaltyLastN.toString()
    }

    function saveValues() {
        Settings.modelName = modelNameField.text;
        Settings.temperature = parseFloat(temperatureField.text) || 0.0;
        Settings.maxTokens = parseInt(maxTokensField.text) || 8192;
        Settings.dryMultiplier = parseFloat(dryMultiplierField.text) || 0.8;
        Settings.dryBase = parseFloat(dryBaseField.text) || 1.75;
        Settings.dryAllowedLength = parseInt(dryAllowedLengthField.text) || 35;
        Settings.dryPenaltyLastN = parseInt(dryRange.text) || 2048;
    }

    Item { Layout.columnSpan: 2; implicitHeight: 4 }

    Label {
        Layout.columnSpan: 2
        text: qsTr("Model name")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
    }
    TextField {
        id: modelNameField
        Layout.columnSpan: 2
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        readOnly: Settings.connectionMode === "managed"
        placeholderText: qsTr("e.g. Unlimited-OCR, or the id your server exposes")
    }
    Label {
        Layout.columnSpan: 2
        visible: Settings.connectionMode === "managed"
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSmall
        color: Theme.textMuted
        text: qsTr("Managed mode: model is \"%1\" — defined by the running server")
                  .arg(Settings.launchModelAlias)
    }

    Label {
        Layout.topMargin: 4
        text: qsTr("Temperature")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
    }
    Label {
        Layout.topMargin: 4
        text: qsTr("Max tokens per page")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
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

    Label {
        Layout.topMargin: 4
        text: qsTr("DRY multiplier")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
    }
    Label {
        Layout.topMargin: 4
        text: qsTr("DRY base")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
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

    Label {
        Layout.topMargin: 4
        text: qsTr("DRY allowed length")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
    }
    Label {
        Layout.topMargin: 4
        text: qsTr("DRY range")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
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

    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSmall
        color: Theme.textMuted
        text: qsTr("DRY (Don't Repeat Yourself) the parameters are selected for optimal recognition accuracy in llama.cpp")
    }

    Item { Layout.fillHeight: true }
}