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

    property string task: ""

    property int titleWidth: 160
    property int endpointWidth: 200
    property int keyWidth: 200
    property int aliasWidth: 150

    // The tab moves the token; the row reloads and saves itself.
    property int reloadToken: 0
    property int commitToken: 0

    property alias endpointText: endpointField.text
    property alias aliasText: aliasField.text

    // The key's presence is not a binding: the secret store has no notify
    // signal, the settings do.
    property bool keyPresent: false

    function load() {
        endpointField.text = Settings.baseUrlForRole(task)
        aliasField.text = Settings.modelNameForRole(task)
        keyCell.load()
        root.keyPresent = Settings.apiKeySetForRole(task)
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
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
    }

    TextField {
        id: endpointField
        Layout.preferredWidth: root.endpointWidth
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        placeholderText: Settings.baseUrl
    }

    SecretKeyField {
        id: keyCell
        Layout.preferredWidth: root.keyWidth
        keySet: root.keyPresent
        commit: function(text) { Settings.setApiKeyForRole(root.task, text) }
        clear: function() { Settings.clearApiKeyForRole(root.task) }
    }

    TextField {
        id: aliasField
        Layout.preferredWidth: root.aliasWidth
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        selectByMouse: true
        placeholderText: qsTr("the model id")
    }
}
