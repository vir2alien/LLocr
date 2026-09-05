import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ColumnLayout {
    id: runtimeScroll
    clip: true
    spacing: 4

    readonly property int connectionMode: connectionModeBox.currentIndex

    function loadValues() {
        rtExternal.loadValues();
        rtInternal.buildInstallOptions();

    }

    function savaValues() {
        rtExternal.savaValues();
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Button {
            text: qsTr("Launch setup wizard…")
            implicitHeight: Theme.controlHeight
            font.pixelSize: Theme.fontCaption
            onClicked: {
                if (dialog.setupWizardRef)
                    dialog.setupWizardRef.startWizard()
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            text: qsTr("Walks you through installing a runtime and a "
                       + "model, then configures the launch.")
        }
    }

    Label {
        text: qsTr("Connection mode")
        font.pixelSize: Theme.fontCaption
        color: Theme.textSecondary
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
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.divider
    }

    RuntimeTabInternal {
        id: rtInternal
        visible: connectionMode === 1
        Layout.fillWidth: true
    }

    Item { implicitHeight: 4 }
    Item { Layout.fillHeight: true }
}