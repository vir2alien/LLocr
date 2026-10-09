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
        connectionTimeoutField.text = Settings.connectionTimeoutMs
        responseTimeoutField.text = Settings.responseTimeoutMs
        rtExternal.loadValues();
        rtInternal.loadValues();
    }

    function saveValues() {
        Settings.connectionTimeoutMs = parseInt(connectionTimeoutField.text) || Settings.connectionTimeoutMs
        Settings.responseTimeoutMs = parseInt(responseTimeoutField.text) || Settings.responseTimeoutMs
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

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true

            LLOLabel {
                text: qsTr("Connection timeout (ms)")
            }
            TextField {
                id: connectionTimeoutField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                selectByMouse: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1000; top: 3600000 }
            }
        }

        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true

            LLOLabel {
                text: qsTr("Request timeout (ms)")
            }
            TextField {
                id: responseTimeoutField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                selectByMouse: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1000; top: 3600000 }
            }
        }
    }

    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.helpColor
        wrapMode: Text.WordWrap
        text: qsTr("Connection timeout — how long to wait for the server to accept a connection. "
                   + "Request timeout — how long to wait for the model's response; raise it if long recognitions are cut off.")
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
