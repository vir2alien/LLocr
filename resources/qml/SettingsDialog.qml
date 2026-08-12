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
    implicitHeight: 320

    onAboutToShow: {
        // Connection
        baseUrlField.text = Settings.baseUrl
        apiKeyField.text  = Settings.apiKey
        timeoutField.text = Settings.connectionTimeoutMs.toString()

        // Model
        modelNameField.text  = Settings.modelName
        temperatureField.text = Settings.temperature.toString()
        maxTokensField.text  = Settings.maxTokens.toString()

        // Output / parser
        var idx = parserBox.model.indexOf(Settings.parserId)
        parserBox.currentIndex = idx >= 0 ? idx : 0
    }

    onAccepted: {
        Settings.baseUrl = baseUrlField.text;
        Settings.apiKey = apiKeyField.text;
        Settings.timeoutMs = parseInt(timeoutField.text) || 3000;
        Settings.modelName = modelNameField.text;
        Settings.temperature = parseFloat(temperatureField.text) || 0.0;
        Settings.maxTokens = parseInt(maxTokensField.text) || 8192;
        Settings.parserId = parserBox.currentText;
        Settings.forceSave();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            TabButton { text: qsTr("Connection") }
            TabButton { text: qsTr("Model") }
            TabButton { text: qsTr("Output") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            ColumnLayout {// Tab 1 — Connection
                spacing: 10

                Label { text: qsTr("Endpoint base URL") }
                TextField {
                    id: baseUrlField
                    Layout.fillWidth: true
                    placeholderText: "http://localhost:8080"
                    selectByMouse: true
                }

                Label { text: qsTr("API key (optional)") }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: apiKeyField
                        Layout.fillWidth: true
                        selectByMouse: true
                        echoMode: revealKey.checked ? TextInput.Normal
                                                    : TextInput.Password
                        placeholderText: qsTr("leave empty for local servers")
                    }
                    CheckBox {
                        id: revealKey
                        text: qsTr("Show")
                    }
                }

                Label { text: qsTr("Request timeout (ms)") }
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

            ColumnLayout {// Tab 2 — Model
                spacing: 10

                Label { text: qsTr("Model name") }
                TextField {
                    id: modelNameField
                    Layout.fillWidth: true
                    selectByMouse: true
                    placeholderText: qsTr("e.g. gpt-4o-mini, or the id your server exposes")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Temperature") }
                        TextField {
                            id: temperatureField
                            Layout.fillWidth: true
                            selectByMouse: true
                            validator: DoubleValidator { bottom: 0.0; top: 2.0; decimals: 2 }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Max tokens") }
                        TextField {
                            id: maxTokensField
                            Layout.fillWidth: true
                            selectByMouse: true
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 1000000 }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }//ColumnLayout Tab 2 — Model

            ColumnLayout {// Tab 3 — Output / parser
                spacing: 10

                Label { text: qsTr("Output parser") }
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
