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

    spacing: Theme.spacingSmall

    signal edited()

    // The load fills the field from the settings: that write is not the
    // user's, so it must not mark the settings window dirty.
    property bool loading: false

    // The wizard's external step shows the shared server only: the tasks are
    // configured in the settings.
    property bool compact: false

    readonly property alias baseUrlText: baseUrlField.text
    // The rows follow the C++ task list (ADR 215): the order is the order the
    // task table shows, and the tab keeps no copy of it.
    readonly property var taskIds: Settings.taskRoles

    readonly property int titleWidth: 120
    readonly property int endpointWidth: 165
    readonly property int keyWidth: 180
    readonly property int aliasWidth: 155

    // One switch for every key of the tab: the shared key and the key of
    // each task. It stands in the table header, where the keys it governs
    // are; the wizard's compact mode has no table, so there the switch
    // stands next to the only key it has.
    property bool revealKeys: false

    property bool sharedKeyPresent: false

    // The rows reload and save themselves when the token moves: a Repeater's
    // item is a plain item for the linter, so the tab cannot call the row's
    // methods by name.
    property int reloadToken: 0
    property int commitToken: 0

    function loadValues() {
        extRoot.loading = true
        baseUrlField.text = Settings.baseUrl
        sharedKeyPresent = Settings.apiKeySet
        sharedKey.load()
        Settings.refreshSecretState()
        reloadToken++
        extRoot.loading = false
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
        font.pixelSize: Theme.bodySize
        color: Theme.textPrimary
    }

    LLOLabel {
        text: qsTr("Endpoint base URL")
        font: Theme.bodySmall
    }
    TextField {
        id: baseUrlField
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        font.pixelSize: Theme.bodySmallSize
        placeholderText: "http://localhost:8080"
        selectByMouse: true
        onTextChanged: if (!extRoot.loading) extRoot.edited()
    }

    LLOLabel {
        id: plaintextKeyWarning
        Layout.fillWidth: true
        font.pixelSize: Theme.footnoteSize
        color: Theme.accentAttention
        visible: Settings.isPlaintextKeyEndpoint(baseUrlField.text)
        wrapMode: Text.WordWrap
        text: qsTr("The key will be sent in plain text (http outside the local machine).")
    }

    // The wizard's compact mode has no task table, so the switch stands
    // next to the only key it has.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        LLOLabel {
            text: qsTr("API key")
            font: Theme.bodySmall
        }
        LLOCheckBox {
            visible: extRoot.compact
            text: qsTr("Show")
            checked: extRoot.revealKeys
            onToggled: extRoot.revealKeys = checked
        }
        Item { Layout.fillWidth: true }
    }
    SecretKeyField {
        id: sharedKey
        Layout.fillWidth: true
        keySet: extRoot.sharedKeyPresent
        revealed: extRoot.revealKeys
        commit: function(text) { Settings.setApiKey(text) }
    }

    LLOLabel {
        Layout.fillWidth: true
        font.pixelSize: Theme.footnoteSize
        color: Theme.helpColor
        text: extRoot.storeNote()
    }

    LLOLabel {
        Layout.fillWidth: true
        font.pixelSize: Theme.footnoteSize
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
        font.pixelSize: Theme.bodySize
        color: Theme.textPrimary
    }

    RowLayout {
        visible: !extRoot.compact
        Layout.fillWidth: true
        spacing: Theme.spacing

        LLOLabel {
            Layout.preferredWidth: extRoot.titleWidth
            font.pixelSize: Theme.footnoteSize
            color: Theme.helpColor
            text: qsTr("Task")
        }
        LLOLabel {
            Layout.preferredWidth: extRoot.endpointWidth
            font.pixelSize: Theme.footnoteSize
            color: Theme.helpColor
            text: qsTr("Endpoint")
        }
        RowLayout {
            Layout.preferredWidth: extRoot.keyWidth
            spacing: Theme.spacingSmall
            LLOLabel {
                font.pixelSize: Theme.footnoteSize
                color: Theme.helpColor
                text: qsTr("API key")
            }
            LLOCheckBox {
                text: qsTr("Show")
                font.pixelSize: Theme.footnoteSize
                checked: extRoot.revealKeys
                onToggled: extRoot.revealKeys = checked
            }
            Item { Layout.fillWidth: true }
        }
        LLOLabel {
            Layout.preferredWidth: extRoot.aliasWidth
            font.pixelSize: Theme.footnoteSize
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
            revealed: extRoot.revealKeys
            reloadToken: extRoot.reloadToken
            commitToken: extRoot.commitToken
        }
    }

    Item { Layout.fillHeight: true }
}
