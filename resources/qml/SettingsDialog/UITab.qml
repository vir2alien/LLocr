import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ColumnLayout {
    spacing: 4

    function loadValues() {
        languageBox.syncFromSettings()
        themeBox.syncFromSettings()
    }

    function saveValues() {
        Settings.language = ["system", "en", "ru"][languageBox.currentIndex];
        UiController.mode = [UiController.System, UiController.Light, UiController.Dark][themeBox.currentIndex];
    }

    LLOLabel {
        text: qsTr("Language")
    }
    ComboBox {
        id: languageBox
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        model: [qsTr("System"), "English", "Русский"]
        function syncFromSettings() {
            var langIdx = ["system", "en", "ru"].indexOf(Settings.language)
            currentIndex = langIdx >= 0 ? langIdx : 0
        }
        onModelChanged: syncFromSettings()
        Component.onCompleted: syncFromSettings()
        Connections {
            target: Settings
            function onLanguageChanged() { languageBox.syncFromSettings() }
        }
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
        function syncFromSettings() {
            var themeIdx = [UiController.System, UiController.Light,
                            UiController.Dark].indexOf(Settings.themeMode)
            currentIndex = themeIdx >= 0 ? themeIdx : 0
        }
        onModelChanged: syncFromSettings()
        Component.onCompleted: syncFromSettings()
        Connections {
            target: Settings
            function onThemeModeChanged() { themeBox.syncFromSettings() }
        }
    }

    Item { Layout.fillHeight: true }
}
