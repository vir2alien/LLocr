pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    title: qsTr("Output settings")
    width: 420
    height: 426
    modality: Qt.NonModal

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    background: SettingsSurface {
    }

    footer: SettingsFooter {
        LLOButton {
            subtle: true
            text: qsTr("Restore defaults")
            font: Theme.bodySmall
            // Guarded setters skip the NOTIFY when a value already equals
            // the default, so re-read the controls explicitly.
            onClicked: {
                Settings.resetOutputDefaults()
                outputTab.loadValues()
            }
        }
        Item { Layout.fillWidth: true }
        LLOButton {
            text: qsTr("Cancel")
            font: Theme.bodySmall
            onClicked: {
                outputTab.loadValues()
                window.close()
            }
        }
        LLOButton {
            emphasis: true
            text: qsTr("Save")
            font: Theme.bodySmall
            onClicked: {
                outputTab.saveValues()
                Settings.forceSave()
                window.close()
            }
        }
    }

    onVisibleChanged: {
        if (visible)
            outputTab.loadValues()
    }

    OutputTab {
        id: outputTab
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
    }
}