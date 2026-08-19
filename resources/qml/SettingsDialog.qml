import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

Dialog {
    id: dialog
    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Save | Dialog.Cancel

    anchors.centerIn: parent
    width: 460
    implicitHeight: 360

    onAboutToShow: {
        //UI
        var langIdx = ["system", "en", "ru"].indexOf(Settings.language)
        languageBox.currentIndex = langIdx >= 0 ? langIdx : 0

        var thtemeIdx = [UiController.System, UiController.Light, UiController.Dark].indexOf(Settings.themeMode)
        themeBox.currentIndex = thtemeIdx >= 0 ? thtemeIdx : 0

        // Connection
        baseUrlField.text = Settings.baseUrl
        apiKeyField.text  = Settings.apiKey
        timeoutField.text = Settings.connectionTimeoutMs.toString()

        // Model
        modelNameField.text  = Settings.modelName
        temperatureField.text = Settings.temperature.toString()
        maxTokensField.text  = Settings.maxTokens.toString()
        dryMultiplierField.text = Settings.dryMultiplier.toString()
        dryBaseField.text = Settings.dryBase.toString()
        dryAllowedLenghField.text = Settings.dryAllowedLength.toString()
        dryRange.text = Settings.dryPenaltyLastN.toString()
        drySequenceBreakersField.text = Settings.drySequenceBreakers

        // Output / parser
        var idx = parserBox.model.indexOf(Settings.parserId)
        parserBox.currentIndex = idx >= 0 ? idx : 0

    }

    onAccepted: {
        Settings.language = ["system", "en", "ru"][languageBox.currentIndex];
        uiController.mode = [UiController.System, UiController.Light, UiController.Dark][themeBox.currentIndex];
        Settings.baseUrl = baseUrlField.text;
        Settings.apiKey = apiKeyField.text;
        Settings.timeoutMs = parseInt(timeoutField.text) || 3000;
        Settings.modelName = modelNameField.text;
        Settings.temperature = parseFloat(temperatureField.text) || 0.0;
        Settings.maxTokens = parseInt(maxTokensField.text) || 16384;
        Settings.dryMultiplier = parseFloat(dryMultiplierField.text) || 0.8;
        Settings.dryBase = parseFloat(dryBaseField.text) || 1.75;
        Settings.dryAllowedLength = parseInt(dryAllowedLenghField.text) || 35;
        Settings.dryPenaltyLastN = parseInt(dryRange.text) || 128;
        Settings.drySequenceBreakers = drySequenceBreakersField.text;
        Settings.parserId = parserBox.currentText;
        Settings.forceSave();
        I18n.setLanguage(Settings.language);
    }

    ColumnLayout {
        anchors.fill: parent
        TabBar {
            id: tabBar
            Layout.fillWidth: true
            TabButton { text: qsTr("UI") }
            TabButton { text: qsTr("Connection") }
            TabButton { text: qsTr("Model") }
            TabButton { text: qsTr("Output") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            ColumnLayout {// Tab 0 - UI
                spacing: 0
                Label {
                    Layout.topMargin: 8
                    text: qsTr("Language")
                }
                ComboBox {
                    id: languageBox
                    Layout.fillWidth: true
                    model: [qsTr("System"), "English", "Русский"]
                }

                Label {
                    Layout.topMargin: 8
                    text: qsTr("Theme")
                }
                ComboBox {
                    id: themeBox
                    Layout.fillWidth: true
                    model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                }
            }// ColumnLayout Tab 0 - UI

            ColumnLayout {// Tab 1 — Connection
                spacing: 0
                Label {
                    Layout.topMargin: 8
                    text: qsTr("Endpoint base URL")
                }
                TextField {
                    id: baseUrlField
                    Layout.fillWidth: true
                    placeholderText: "http://localhost:8080"
                    selectByMouse: true
                }

                Label {
                    Layout.topMargin: 8
                    text: qsTr("API key (optional)")
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: apiKeyField
                        Layout.fillWidth: true
                        selectByMouse: true
                        echoMode: revealKey.checked ? TextInput.Normal
                                                    : TextInput.Password
                    }
                    CheckBox {
                        id: revealKey
                        text: qsTr("Show")
                    }
                }

                Label {
                    Layout.topMargin: 8
                    text: qsTr("Request timeout (ms)")
                }
                TextField {
                    id: timeoutField
                    Layout.fillWidth: true
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1000; top: 3600000 }
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    text: qsTr("Note: the API key is stored locally in plaintext. "
                               + "Avoid using production keys.")
                }

                Item { Layout.fillHeight: true }  // push content to the top
            }//ColumnLayout Tab 1 — Connection

            GridLayout {// Tab 2 — Model
                columns: 2
                rowSpacing: 0
                columnSpacing: 8

                Label {
                    Layout.columnSpan: 2
                    text: qsTr("Model name")
                }
                TextField {
                    id: modelNameField
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    selectByMouse: true
                    placeholderText: qsTr("e.g. Unlimited-OCR, or the id your server exposes")
                }

                Label {
                    Layout.topMargin: 8
                    text: qsTr("Temperature")
                }
                Label {
                    Layout.topMargin: 8
                    text: qsTr("Max tokens per page")
                }
                TextField {
                    id: temperatureField
                    Layout.fillWidth: true
                    selectByMouse: true
                    validator: DoubleValidator { bottom: 0.0; top: 2.0; decimals: 2 }
                }
                TextField {
                    id: maxTokensField
                    Layout.fillWidth: true
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 1000000 }
                }

                Label {
                    Layout.topMargin: 8
                    text: qsTr("DRY multiplier")
                }
                Label {
                    Layout.topMargin: 8
                    text: qsTr("DRY base")
                }
                TextField {
                    id: dryMultiplierField
                    Layout.fillWidth: true
                    selectByMouse: true
                    validator: DoubleValidator { bottom: 0.0; top: 2.0; decimals: 2 }
                }
                TextField {
                    id: dryBaseField
                    Layout.fillWidth: true
                    selectByMouse: true
                    validator: DoubleValidator { bottom: 0.0; top: 3.0; decimals: 2 }
                }

                Label {
                    Layout.topMargin: 8
                    text: qsTr("DRY allowed length")
                }
                Label {
                    Layout.topMargin: 8
                    text: qsTr("DRY range")
                }
                TextField {
                    id: dryAllowedLenghField
                    Layout.fillWidth: true
                    selectByMouse: true
                    validator: IntValidator { bottom: 0;}
                }
                TextField {
                    id: dryRange
                    Layout.fillWidth: true
                    selectByMouse: true
                    validator: IntValidator { bottom: 0;}
                }

                Label {
                    Layout.topMargin: 8
                    text: qsTr("DRY sequence breakers")
                }
                Item {
                    Layout.topMargin: 8
                    Layout.fillWidth: true
                }
                TextField {
                    id: drySequenceBreakersField
                    Layout.fillWidth: true
                    selectByMouse: true
                }
                Item {
                    Layout.fillWidth: true
                }

                Label {
                    Layout.columnSpan: parent.columns
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    text: qsTr("DRY (Don't Repeat Yourself) the parameters are selected for optimal recognition accuracy in llama.cpp")
                }

                Item { Layout.fillHeight: true }
            }//ColumnLayout Tab 2 — Model

            ColumnLayout {// Tab 3 — Output / parser
                spacing: 0
                Label {
                    Layout.topMargin: 8
                    text: qsTr("Output parser")
                }
                ComboBox {
                    id: parserBox
                    Layout.fillWidth: true
                    model: controller.parserNames
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    text: qsTr("‘raw’ keeps the model text as-is. ‘det_tokens’ extracts "
                               + "positioned fragments (bounding boxes) for the overlay.")
                }

                Item { Layout.fillHeight: true }
            }//ColumnLayout Tab 3 — Output / parser
        }
    }
}
