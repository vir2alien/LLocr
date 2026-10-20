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
    property bool valuesLoaded: false

    // Called by the wizard's Back: hides the step without committing the
    // typed endpoint, so re-entering the step shows the saved settings again.
    function revertValues() {
        valuesLoaded = false
        rtExternal.loadValues()
    }

    onVisibleChanged: {
        if (visible) {
            rtExternal.loadValues()
            valuesLoaded = true
        } else if (valuesLoaded) {
            valuesLoaded = false
            rtExternal.saveValues()
        }
    }

    implicitHeight: contentColumn.implicitHeight + 40

    ColumnLayout {
        id: contentColumn
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
