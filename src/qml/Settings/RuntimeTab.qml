pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ColumnLayout {
    id: runtimeLayout
    clip: true
    spacing: Theme.spacingSmall

    signal edited()

    // The load fills the fields from the settings: the changes it makes are
    // not the user's, so they must not mark the settings window dirty.
    property bool loading: false

    Layout.maximumHeight: Number.POSITIVE_INFINITY

    readonly property bool externalMode: Settings.connectionMode === "external"

    function loadValues() {
        runtimeLayout.loading = true
        connectionTimeoutField.text = Settings.connectionTimeoutMs
        responseTimeoutField.text = Settings.responseTimeoutMs
        rtExternal.loadValues();
        rtInternal.loadValues();
        runtimeLayout.loading = false
    }

    function saveValues() {
        Settings.connectionTimeoutMs = parseInt(connectionTimeoutField.text) || Settings.connectionTimeoutMs
        Settings.responseTimeoutMs = parseInt(responseTimeoutField.text) || Settings.responseTimeoutMs
        rtExternal.saveValues();
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        LLOLabel {
            text: qsTr("Connection mode")
            font: Theme.body
        }
        SegmentedControl {
            currentIndex: Settings.connectionMode === "managed" ? 1 : 0
            options: [
                { label: qsTr("External") },
                { label: qsTr("Managed") }
            ]
            onActivated: (idx) => {
                Settings.connectionMode = (idx === 1) ? "managed" : "external"
                runtimeLayout.edited()
            }
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMedium

        ColumnLayout {
            spacing: Theme.spacingSmall
            Layout.fillWidth: true

            LLOLabel {
                text: qsTr("Connection timeout (ms)")
                font: Theme.body
            }
            TextField {
                id: connectionTimeoutField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.bodySize
                selectByMouse: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1000; top: 3600000 }
                onTextChanged: if (!runtimeLayout.loading) runtimeLayout.edited()
            }
        }

        ColumnLayout {
            spacing: Theme.spacingSmall
            Layout.fillWidth: true

            LLOLabel {
                text: qsTr("Request timeout (ms)")
                font: Theme.body
            }
            TextField {
                id: responseTimeoutField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.bodySize
                selectByMouse: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1000; top: 3600000 }
                onTextChanged: if (!runtimeLayout.loading) runtimeLayout.edited()
            }
        }
    }

    LLOLabel {
        Layout.fillWidth: true
        font.pixelSize: Theme.footnoteSize
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
        onEdited: runtimeLayout.edited()
    }

    RuntimeTabInternal {
        id: rtInternal
        visible: !runtimeLayout.externalMode
        Layout.fillWidth: true
        Layout.fillHeight: true
        onEdited: runtimeLayout.edited()
    }

    Item {
        visible: rtExternal.visible
        Layout.fillHeight: true
    }
}
