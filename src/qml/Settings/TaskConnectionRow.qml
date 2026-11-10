pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// One task of the external connection table: its own endpoint, its own key
// and its own model alias. An empty endpoint or an empty key means the
// shared value (ADR 226).
RowLayout {
    id: root

    signal edited()

    property string task: ""

    property int titleWidth: 160
    property int endpointWidth: 200
    property int keyWidth: 200
    property int aliasWidth: 150

    // The tab owns the "Show" switch: one switch reveals every key.
    property bool revealed: false

    // The tab moves the token; the row reloads and saves itself.
    property int reloadToken: 0
    property int commitToken: 0

    // The load fills the fields from the settings: that write is not the
    // user's, so it must not mark the settings window dirty.
    property bool loading: false

    property alias endpointText: endpointField.text
    property alias aliasText: aliasField.text

    readonly property alias plaintextKeyWarningVisible: plaintextKeyWarning.visible

    // The key's presence is not a binding: the secret store has no notify
    // signal, the settings do.
    property bool keyPresent: false

    function load() {
        root.loading = true
        endpointField.text = Settings.baseUrlForRole(task)
        aliasField.text = Settings.modelNameForRole(task)
        root.keyPresent = Settings.apiKeySetForRole(task)
        keyCell.load()
        root.loading = false
    }

    function save() {
        Settings.setBaseUrlForRole(task, endpointField.text)
        Settings.setModelNameForRole(task, aliasField.text)
        keyCell.save()
    }

    onReloadTokenChanged: root.load()
    onCommitTokenChanged: root.save()

    Component.onCompleted: root.load()

    Connections {
        target: Settings
        function onSecretsChanged() {
            root.keyPresent = Settings.apiKeySetForRole(root.task)
        }
    }

    LLOLabel {
        Layout.preferredWidth: root.titleWidth
        Layout.fillHeight: true
        text: RoleProfiles.title(root.task)
        font.pixelSize: Theme.bodySmallSize
        color: Theme.textPrimary
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
    }

    ColumnLayout {
        Layout.preferredWidth: root.endpointWidth
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        TextField {
            id: endpointField
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            font.pixelSize: Theme.bodySmallSize
            selectByMouse: true
            placeholderText: Settings.baseUrl
            onTextChanged: if (!root.loading) root.edited()
        }

        LLOLabel {
            id: plaintextKeyWarning
            Layout.fillWidth: true
            font.pixelSize: Theme.footnoteSize
            color: Theme.accentAttention
            visible: Settings.isPlaintextKeyEndpoint(endpointField.text)
            wrapMode: Text.WordWrap
            text: qsTr("The key will be sent in plain text (http outside the local machine).")
        }
    }

    SecretKeyField {
        id: keyCell
        Layout.preferredWidth: root.keyWidth
        keySet: root.keyPresent
        revealed: root.revealed
        commit: function(text) { Settings.setApiKeyForRole(root.task, text) }
    }

    TextField {
        id: aliasField
        Layout.preferredWidth: root.aliasWidth
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        font.pixelSize: Theme.bodySmallSize
        selectByMouse: true
        placeholderText: qsTr("the model id")
        onTextChanged: if (!root.loading) root.edited()
    }
}
