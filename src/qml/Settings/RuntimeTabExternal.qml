pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// The external server: one shared connection and, per task, its own
// endpoint, key and model alias (ADR 226). An empty task field means the
// shared value.
ColumnLayout {
    id: extRoot

    spacing: 4

    // The wizard's external step shows the shared server only: the tasks are
    // configured in the settings.
    property bool compact: false

    readonly property alias baseUrlText: baseUrlField.text
    readonly property var taskIds: ["pageOcr", "rawPageOcr", "blockRecognition", "decision", "layout", "fix"]

    readonly property int titleWidth: 160
    readonly property int endpointWidth: 190
    readonly property int keyWidth: 190
    readonly property int aliasWidth: 140

    property bool sharedKeyPresent: false

    // The rows reload and save themselves when the token moves: a Repeater's
    // item is a plain item for the linter, so the tab cannot call the row's
    // methods by name.
    property int reloadToken: 0
    property int commitToken: 0

    function loadValues() {
        baseUrlField.text = Settings.baseUrl
        sharedKey.load()
        sharedKeyPresent = Settings.apiKeySet
        Settings.refreshSecretState()
        reloadToken++
    }

    function saveValues() {
        Settings.baseUrl = baseUrlField.text
        sharedKey.save()
        commitToken++
    }

    // Where the key lives, said plainly.
    function storeNote() {
        if (!Settings.secretStoreAvailable)
            return qsTr("There is no system secret store on this machine: the key is kept in the memory until the app is closed.")
        if (!Settings.lastKeyPersisted)
            return qsTr("The system store refused the key: it is kept in the memory until the app is closed.")
        return qsTr("The key is kept in %1 — not in the settings file.").arg(Settings.secretStoreName)
    }

    Connections {
        target: Settings
        function onSecretsChanged() {
            extRoot.sharedKeyPresent = Settings.apiKeySet
        }
    }

    LLOLabel {
        text: qsTr("Shared server — the default for every task")
        font.bold: true
        color: Theme.textPrimary
    }

    LLOLabel {
        text: qsTr("Endpoint base URL")
    }
    TextField {
        id: baseUrlField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        placeholderText: "http://localhost:8080"
        selectByMouse: true
    }

    LLOLabel {
        text: qsTr("API key")
    }
    SecretKeyField {
        id: sharedKey
        Layout.fillWidth: true
        keySet: extRoot.sharedKeyPresent
        commit: function(text) { Settings.setApiKey(text) }
        clear: function() { Settings.clearApiKey() }
    }

    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.helpColor
        text: extRoot.storeNote()
    }

    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.helpColor
        visible: Settings.secretError.length > 0
        wrapMode: Text.WordWrap
        text: qsTr("The system store did not answer: %1").arg(Settings.secretError)
    }

    Item {
        visible: !extRoot.compact
        Layout.fillWidth: true
        implicitHeight: visible ? Theme.spacingLarge : 0
    }

    LLOLabel {
        visible: !extRoot.compact
        text: qsTr("Tasks — an empty field takes the shared value")
        font.bold: true
        color: Theme.textPrimary
    }

    RowLayout {
        visible: !extRoot.compact
        Layout.fillWidth: true
        spacing: Theme.spacing

        LLOLabel {
            Layout.preferredWidth: extRoot.titleWidth
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("Task")
        }
        LLOLabel {
            Layout.preferredWidth: extRoot.endpointWidth
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("Endpoint")
        }
        LLOLabel {
            Layout.preferredWidth: extRoot.keyWidth
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("API key")
        }
        LLOLabel {
            Layout.preferredWidth: extRoot.aliasWidth
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("Model alias")
        }
    }

    Repeater {
        id: taskRows
        model: extRoot.compact ? [] : extRoot.taskIds
        delegate: TaskConnectionRow {
            required property var modelData

            task: modelData
            Layout.fillWidth: true
            titleWidth: extRoot.titleWidth
            endpointWidth: extRoot.endpointWidth
            keyWidth: extRoot.keyWidth
            aliasWidth: extRoot.aliasWidth
            reloadToken: extRoot.reloadToken
            commitToken: extRoot.commitToken
        }
    }

    Item { Layout.fillHeight: true }
}
