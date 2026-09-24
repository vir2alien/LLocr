pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Welcome to LLM OCR")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("This assistant recognizes text from images and PDFs using a "
                       + "local LLM. A few steps will configure the app — you can "
                       + "change everything later in Settings.")
        }

        Item { implicitHeight: 6 }

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
            onActivated: (idx) => {
                Settings.language = ["system", "en", "ru"][idx]
                I18n.setLanguage(Settings.language)
            }
            Connections {
                target: Settings
                function onLanguageChanged() { languageBox.syncFromSettings() }
            }
        }

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
            onActivated: (idx) => {
                UiController.mode = [UiController.System, UiController.Light,
                                     UiController.Dark][idx]
            }
            Connections {
                target: Settings
                function onThemeModeChanged() { themeBox.syncFromSettings() }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
