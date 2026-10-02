pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ColumnLayout {
    id: runtimeLayout
    clip: true
    spacing: 4

    Layout.maximumHeight: Number.POSITIVE_INFINITY

    readonly property bool externalMode: Settings.connectionMode === "external"

    function loadValues() {
        rtExternal.loadValues();
        rtInternal.loadValues();
    }

    function saveValues() {
        rtExternal.saveValues();
    }

    LLOLabel {
        text: qsTr("Connection mode")
    }

    ComboBox {
        id: connectionModeBox
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        textRole: "text"
        model: [
            { value: "external", text: qsTr("External server") },
            { value: "managed", text: qsTr("Managed local server") }
        ]
        function syncMode() {
            currentIndex = Settings.connectionMode === "managed" ? 1 : 0
        }
        onActivated: (idx) => {
            Settings.connectionMode = (idx === 1) ? "managed" : "external"
        }
        onModelChanged: syncMode()
        Component.onCompleted: syncMode()
        Connections {
            target: Settings
            function onConnectionModeChanged() { connectionModeBox.syncMode() }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.divider
    }

    RuntimeTabExternal {
        id: rtExternal
        visible: runtimeLayout.externalMode
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    RuntimeTabInternal {
        id: rtInternal
        visible: !runtimeLayout.externalMode
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    Item {
        visible: rtExternal.visible
        Layout.fillHeight: true
    }
}
