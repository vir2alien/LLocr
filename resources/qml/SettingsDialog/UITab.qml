import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ColumnLayout {
    spacing: 4

    function loadValues() {
        var langIdx = ["system", "en", "ru"].indexOf(Settings.language)
        languageBox.currentIndex = langIdx >= 0 ? langIdx : 0

        var themeIdx = [UiController.System, UiController.Light, UiController.Dark].indexOf(Settings.themeMode)
        themeBox.currentIndex = themeIdx >= 0 ? themeIdx : 0
    }

    function savaValues() {
        Settings.language = ["system", "en", "ru"][languageBox.currentIndex];
        uiController.mode = [UiController.System, UiController.Light, UiController.Dark][themeBox.currentIndex];
    }

    LLOLabel {
        text: qsTr("Language")
    }
    ComboBox {
        id: languageBox
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        model: [qsTr("System"), "English", "Русский"]
    }

    Item { implicitHeight: 6 }

    LLOLabel {
        text: qsTr("Theme")
    }
    ComboBox {
        id: themeBox
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
    }

    Item { Layout.fillHeight: true }
}