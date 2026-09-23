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

    footer: Rectangle {
        implicitHeight: footerRow.implicitHeight + 2 * Theme.spacingLarge
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.divider
        }

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.leftMargin: Theme.paddingWindow
            anchors.rightMargin: Theme.paddingWindow
            anchors.topMargin: Theme.spacingLarge
            anchors.bottomMargin: Theme.spacingLarge
            spacing: Theme.spacing

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
    }

    UITab {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
    }
}