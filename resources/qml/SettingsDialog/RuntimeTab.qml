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

    readonly property int connectionMode: connectionModeBox.currentIndex

    function loadValues() {
        rtExternal.loadValues();
        rtInternal.buildInstallOptions();
    }

    function saveValues() {
        rtExternal.savaValues();
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        LLOButton {
            text: qsTr("Launch setup wizard…")
            onClicked: {
                if (dialog.setupWizardRef)
                    dialog.setupWizardRef.startWizard()
            }
        }
        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
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
            { value: "managed", text: qsTr("Managed local server") }
        ]
        onActivated: (idx) => Settings.connectionMode = model[idx].value
        Component.onCompleted:
            currentIndex = Settings.connectionMode === "managed" ? 1 : 0
        Connections {
            target: Settings
            function onConnectionModeChanged() {
                connectionModeBox.currentIndex =
                    Settings.connectionMode === "managed" ? 1 : 0
            }
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
        visible: connectionMode === 1
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    Item {
        visible: rtExternal.visible
        Layout.fillHeight: true
    }
}