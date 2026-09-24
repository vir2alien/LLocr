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

    property var setupWizardRef: null
    property var logWindowRef: null
    // Whether the managed-runtime actions (Start/Stop/Restart) are allowed —
    // passed down from the owning window instead of relying on the
    // instantiation-context lookup.
    property bool canManage: false

    Layout.maximumHeight: Number.POSITIVE_INFINITY

    readonly property int connectionMode: connectionModeBox.currentIndex

    function loadValues() {
        rtExternal.loadValues();
        rtInternal.loadValues();
    }

    function saveValues() {
        rtExternal.saveValues();
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        LLOButton {
            text: qsTr("Launch setup wizard…")
            onClicked: {
                if (runtimeLayout.setupWizardRef)
                    runtimeLayout.setupWizardRef.startWizard()
            }
        }
        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("Walks you through installing a runtime and a "
                       + "model, then configures the launch.")
        }
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
            { value: "managed", text: qsTr("Managed local server (Specify llama.cpp binary)") },
            { value: "managed-download", text: qsTr("Managed local server (Download llama.cpp via app)") }
        ]
        function syncMode() {
            if (Settings.connectionMode === "managed")
                currentIndex = Settings.serverPathIsManaged ? 2 : 1
            else
                currentIndex = 0
        }
        onActivated: (idx) => {
            if (idx === 0) {
                Settings.connectionMode = "external"
            } else {
                Settings.connectionMode = "managed"
                Settings.serverPathIsManaged = (idx === 2)
            }
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
        visible: connectionMode === 0
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    RuntimeTabInternal {
        id: rtInternal
        downloadMode: connectionMode === 2
        visible: connectionMode !== 0
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    Item {
        visible: rtExternal.visible
        Layout.fillHeight: true
    }
}