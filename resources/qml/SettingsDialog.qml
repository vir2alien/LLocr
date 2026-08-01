import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Save | Dialog.Cancel

    anchors.centerIn: parent
    width: 560
    implicitHeight: 520

    onAboutToShow: {
        // Connection
        baseUrlField.text = controller.baseUrl
        apiKeyField.text  = controller.apiKey
        timeoutField.text = controller.timeoutMs.toString()

        // Model
        modelNameField.text  = controller.modelName
        promptArea.text      = controller.prompt
        temperatureField.text = controller.temperature.toString()
        maxTokensField.text  = controller.maxTokens.toString()

        // Output / parser
        var idx = parserBox.model.indexOf(controller.parserId)
        parserBox.currentIndex = idx >= 0 ? idx : 0
        bboxRangeField.text = controller.bboxCoordinateRange.toString()
    }

    onAccepted: {
        controller.applySettings({
            "baseUrl":  baseUrlField.text,
            "apiKey":   apiKeyField.text,
            "timeoutMs": parseInt(timeoutField.text) || controller.timeoutMs,

            "modelName": modelNameField.text,
            "prompt":    promptArea.text,
            "temperature": parseFloat(temperatureField.text) || 0.0,
            "maxTokens":  parseInt(maxTokensField.text) || controller.maxTokens,

            "parserId":  parserBox.currentText,
            "bboxCoordinateRange": parseInt(bboxRangeField.text) || controller.bboxCoordinateRange
        })
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
                    font.pixelSize: 11
                    color: "#999999"
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

                Label { text: qsTr("Prompt / instruction") }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    TextArea {
                        id: promptArea
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        placeholderText: qsTr("OCR this document. Return the text.")
                    }
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
                    font.pixelSize: 11
                    color: "#999999"
                    text: qsTr("‘raw’ keeps the model text as-is. ‘det_tokens’ extracts "
                               + "positioned fragments (bounding boxes) for the overlay.")
                }

                Label { text: qsTr("Bbox coordinate range") }
                TextField {
                    id: bboxRangeField
                    Layout.fillWidth: true
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 100000 }
                    enabled: parserBox.currentText === "det_tokens"
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 11
                    color: "#999999"
                    text: qsTr("Scale of the raw coordinates a bbox model reports "
                               + "(coordinates are treated as 0..range). Only used by "
                               + "positional parsers.")
                }

                Item { Layout.fillHeight: true }
            }//ColumnLayout Tab 3 — Output / parser
        }
    }
}
