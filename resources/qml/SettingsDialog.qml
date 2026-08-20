import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

Dialog {
    id: dialog
    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Save | Dialog.RestoreDefaults | Dialog.Cancel

    anchors.centerIn: parent
    width: 480
    implicitHeight: 380

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    header: Item {
        implicitHeight: 38
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: dialog.title
            font.bold: true
            font.pixelSize: Theme.fontNormal
            color: Theme.textPrimary
        }
    }

    footer: DialogButtonBox {
        background: Rectangle {
            color: "transparent"
        }
        alignment: Qt.AlignRight
        standardButtons: dialog.standardButtons
        spacing: 6
        padding: 10
    }

    function loadValues() {
        // UI
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

        // Output / parser
        var idx = parserBox.model.indexOf(Settings.parserId)
        parserBox.currentIndex = idx >= 0 ? idx : 0
    }

    onAboutToShow: {
        loadValues()
    }

    onReset: {
        Settings.resetToDefaults();
        loadValues();
    }

    onAccepted: {
        Settings.language = ["system", "en", "ru"][languageBox.currentIndex];
        uiController.mode = [UiController.System, UiController.Light, UiController.Dark][themeBox.currentIndex];
        Settings.baseUrl = baseUrlField.text;
        Settings.apiKey = apiKeyField.text;
        Settings.connectionTimeoutMs = parseInt(timeoutField.text) || 120000;
        Settings.modelName = modelNameField.text;
        Settings.temperature = parseFloat(temperatureField.text) || 0.0;
        Settings.maxTokens = parseInt(maxTokensField.text) || 16384;
        Settings.dryMultiplier = parseFloat(dryMultiplierField.text) || 0.8;
        Settings.dryBase = parseFloat(dryBaseField.text) || 1.75;
        Settings.dryAllowedLength = parseInt(dryAllowedLenghField.text) || 35;
        Settings.dryPenaltyLastN = parseInt(dryRange.text) || 128;
        Settings.parserId = parserBox.currentText;
        Settings.forceSave();
        I18n.setLanguage(Settings.language);
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            implicitHeight: 28

            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.divider
                }
            }

            component CustomTabButton: TabButton {
                id: tabBtn
                implicitHeight: 28
                padding: 4
                contentItem: Text {
                    text: tabBtn.text
                    font.pixelSize: Theme.fontCaption
                    color: tabBtn.checked ? Theme.textPrimary : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                }
                background: Rectangle {
                    color: tabBtn.checked ? Theme.surface : Theme.surfaceSunken
                    border.color: Theme.divider
                    border.width: 1
                    Rectangle {
                        visible: tabBtn.checked
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: Theme.surface
                    }
                }
            }

            CustomTabButton { text: qsTr("UI") }
            CustomTabButton { text: qsTr("Connection") }
            CustomTabButton { text: qsTr("Model") }
            CustomTabButton { text: qsTr("Output") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            ColumnLayout { // Tab 0 - UI
                spacing: 4
                Label {
                    text: qsTr("Language")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                }
                ComboBox {
                    id: languageBox
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    model: [qsTr("System"), "English", "Русский"]
                }

                Item { implicitHeight: 6 }

                Label {
                    text: qsTr("Theme")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                }
                ComboBox {
                    id: themeBox
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                }

                Item { Layout.fillHeight: true }
            }

            ColumnLayout { // Tab 1 — Connection
                spacing: 4
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

                Item { Layout.fillHeight: true }
            }

            GridLayout { // Tab 2 — Model
                columns: 2
                rowSpacing: 4
                columnSpacing: 8

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
                    placeholderText: qsTr("e.g. Unlimited-OCR, or the id your server exposes")
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
                    id: dryAllowedLenghField
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

            ColumnLayout { // Tab 3 — Output / parser
                spacing: 4
                Label {
                    text: qsTr("Output parser")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                }
                ComboBox {
                    id: parserBox
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    model: controller.parserNames
                }

                Item { implicitHeight: 6 }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    text: qsTr("‘raw’ keeps the model text as-is. ‘det_tokens’ extracts "
                               + "positioned fragments (bounding boxes) for the overlay.")
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
