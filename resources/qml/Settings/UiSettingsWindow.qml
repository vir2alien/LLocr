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

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    // Interface settings apply immediately, so there is no Save/Cancel pair;
    // the footer only offers a scoped reset.
    footer: ToolBar {
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: Theme.spacingSmall

            LLOButton {
                text: qsTr("Restore defaults")
                onClicked: {
                    Settings.language = "system"
                    I18n.setLanguage(Settings.language)
                    UiController.mode = UiController.System
                }
            }
            Item { Layout.fillWidth: true }
        }
    }

    UITab {
        anchors.fill: parent
        anchors.margins: 12
    }
}