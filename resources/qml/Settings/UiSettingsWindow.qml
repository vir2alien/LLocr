pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    title: qsTr("Interface settings")
    width: 440
    height: 340
    modality: Qt.NonModal

    background: SettingsSurface {
    }

    footer: SettingsFooter {
        LLOButton {
            subtle: true
            text: qsTr("Restore defaults")
            onClicked: {
                Settings.language = "system"
                I18n.setLanguage(Settings.language)
                UiController.mode = UiController.System
            }
        }
        Item { Layout.fillWidth: true }
    }

    UITab {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
    }
}