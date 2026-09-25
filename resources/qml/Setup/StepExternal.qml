pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"
import "../Settings"

Item {
    id: root

    property bool complete: rtExternal.baseUrlText.trim().length > 0

    // Guards the hide path: the wizard instantiates every step eagerly, so
    // the initial `visible: false` binding evaluation fires onVisibleChanged
    // before loadValues() ever ran — saving then would wipe the persisted
    // external settings with empty fields.
    property bool valuesLoaded: false

    onVisibleChanged: {
        if (visible) {
            rtExternal.loadValues()
            valuesLoaded = true
        } else if (valuesLoaded) {
            valuesLoaded = false
            rtExternal.saveValues()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("External server")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Configure the connection to your OpenAI-compatible server.")
        }

        RuntimeTabExternal {
            id: rtExternal
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }
    }
}
