pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// One task of the external connection: the model name in the row, its own
// endpoint and key behind the gear (ADR 226). An empty endpoint or an empty
// key means the shared value.
Item {
    id: root

    implicitHeight: row.implicitHeight

    signal edited()

    property string task: ""

    property int titleWidth: 140

    // The tab owns the "Show" switch: one switch reveals every key.
    property bool revealed: false

    // The tab moves the token; the row reloads and saves itself.
    property int reloadToken: 0
    property int commitToken: 0

    // The load fills the fields from the settings: that write is not the
    // user's, so it must not mark the settings window dirty.
    property bool loading: false

    // The drafts of the row: the model name, the endpoint and the key the
    // user typed. The popup edits the last two.
    property string aliasText: ""
    property string endpointText: ""
    property string keyDraft: ""
    property bool keyEdited: false
    property bool keyPresent: false

    readonly property bool plaintextKeyWarningVisible: Settings.isPlaintextKeyEndpoint(root.endpointText)

    function load() {
        root.loading = true
        root.aliasText = Settings.modelNameForRole(task)
        root.endpointText = Settings.baseUrlForRole(task)
        root.keyPresent = Settings.apiKeySetForRole(task)
        root.keyDraft = ""
        root.keyEdited = false
        aliasField.text = root.aliasText
        root.loading = false
    }

    function save() {
        Settings.setModelNameForRole(task, root.aliasText)
        Settings.setBaseUrlForRole(task, root.endpointText)
        if (root.keyEdited)
            Settings.setApiKeyForRole(task, root.keyDraft)
    }

    onReloadTokenChanged: root.load()
    onCommitTokenChanged: root.save()

    Component.onCompleted: root.load()

    // The key's presence is not a binding: the secret store has no notify
    // signal, the settings do.
    Connections {
        target: Settings
        function onSecretsChanged() {
            root.keyPresent = Settings.apiKeySetForRole(root.task)
        }
    }

    // The row itself is a layout; the popup is a sibling of it, not a child:
    // a popup inside a layout would be sized and placed by the layout.
    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Theme.spacingSmall

        LLOLabel {
            Layout.preferredWidth: root.titleWidth
            Layout.fillHeight: true
            text: RoleProfiles.title(root.task)
            font.pixelSize: Theme.bodySmallSize
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
        }

        TextField {
            id: aliasField
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            font.pixelSize: Theme.bodySmallSize
            selectByMouse: true
            placeholderText: qsTr("the model id")
            onTextEdited: {
                root.aliasText = aliasField.text
                root.edited()
            }
        }

        ToolButton {
            id: gearButton

            implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
            implicitHeight: Math.max(Theme.controlHeight, topPadding + bottomPadding + contentItem.implicitHeight)
            onClicked: endpointPopup.open()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Endpoint and key")

            contentItem: Item {
                implicitWidth: gearIcon.size
                implicitHeight: gearIcon.size

                Icon {
                    id: gearIcon
                    anchors.centerIn: parent
                    name: "settings"
                    size: Theme.iconSizeMedium
                    color: gearButton.enabled ? Theme.textPrimary : Theme.textMuted
                }
            }
        }
    }

    Popup {
        id: endpointPopup

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 420
        padding: Theme.spacing
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
            radius: Theme.controlRadius
        }

        contentItem: TaskEndpointForm {
            taskTitle: RoleProfiles.title(root.task)
            revealed: root.revealed
            endpointDraft: root.endpointText
            keyDraft: root.keyDraft
            keyEdited: root.keyEdited
            keyPresent: root.keyPresent
            onEdited: (endpoint, key, keyIsEdited) => {
                root.endpointText = endpoint
                root.keyDraft = key
                root.keyEdited = keyIsEdited
                root.edited()
            }
            onCloseRequested: endpointPopup.close()
        }
    }
}
