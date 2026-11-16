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

    spacing: Theme.spacing

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

    readonly property int titleWidth: 140

    // One switch for every key of the tab: the shared key and the key of
    // each task. It stands in the shared server block, where the key it
    // governs is; the wizard's compact mode has no task table, so the switch
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
        // The answer of the last check belongs to the address of that time.
        ConnectionProbe.reset()
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

    // What the system store answered, said the way the user can act on it.
    // The store without a key is the normal state of an optional key: only a
    // refusal (or a failure) is a badge (ADR 233).
    function storeError() {
        if (!Settings.secretStoreAvailable)
            return qsTr("No system secret store")
        if (Settings.secretStatus === "refused" || Settings.secretStatus === "failed")
            return qsTr("The system store did not answer")
        if (sharedKeyPresent && !Settings.lastKeyPersisted)
            return qsTr("The system store refused the key")
        return ""
    }

    // The check of the shared server: the answer comes from the runtime
    // layer, so the UI does not touch the network itself.
    function checkServer() {
        ConnectionProbe.probe(baseUrlField.text, sharedKey.plainText(sharedKey.text))
    }

    function checkStatusText() {
        if (ConnectionProbe.running)
            return qsTr("Checking the server…")
        if (ConnectionProbe.ok) {
            let text = qsTr("Connected · %1 ms").arg(ConnectionProbe.latencyMs)
            if (ConnectionProbe.modelCount > 0)
                text += " · " + qsTr("%1 models").arg(ConnectionProbe.modelCount)
            return text
        }
        if (ConnectionProbe.errorText.length === 0)
            return ""
        return qsTr("No answer: %1").arg(ConnectionProbe.errorText)
    }

    // The shared server is a card of its own: the tasks below it are one
    // thing, the connection they share is another.
    Rectangle {
        id: sharedCard

        Layout.fillWidth: true
        implicitHeight: sharedColumn.implicitHeight + Theme.spacing * 2
        color: Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1
        radius: Theme.controlRadius + 2

        ColumnLayout {
            id: sharedColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing
            spacing: Theme.spacingSmall

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingTiny

                    LLOLabel {
                        text: qsTr("Shared server")
                        font.pixelSize: Theme.bodySize
                        font.bold: true
                        color: Theme.textPrimary
                    }
                    LLOLabel {
                        text: qsTr("The default for every task")
                        font.pixelSize: Theme.footnoteSize
                        color: Theme.helpColor
                    }
                }

                IconToolButton {
                    text: qsTr("Check")
                    iconName: "rotate-cw"
                    enabled: !ConnectionProbe.running
                    onClicked: extRoot.checkServer()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                visible: extRoot.checkStatusText().length > 0

                Icon {
                    name: ConnectionProbe.ok ? "circle-check" : "info"
                    size: Theme.iconSizeSmall
                    color: ConnectionProbe.ok ? Theme.success : Theme.error
                }
                LLOLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.footnoteSize
                    color: ConnectionProbe.ok ? Theme.helpColor : Theme.error
                    text: extRoot.checkStatusText()
                }
            }

            // The labels stand over their fields: the two fields share one
            // row of a grid, so they begin at the same height — a checkbox in
            // the header of one of them would push its field down.
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spacingMedium
                rowSpacing: Theme.spacingSmall

                LLOLabel {
                    text: qsTr("Endpoint base URL")
                    font.pixelSize: Theme.bodySmallSize
                    color: Theme.textPrimary
                    verticalAlignment: Text.AlignVCenter
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing

                    LLOLabel {
                        Layout.fillWidth: true
                        text: qsTr("API key")
                        font.pixelSize: Theme.bodySmallSize
                        color: Theme.textPrimary
                        verticalAlignment: Text.AlignVCenter
                    }
                    LLOCheckBox {
                        text: qsTr("Show")
                        font.pixelSize: Theme.footnoteSize
                        checked: extRoot.revealKeys
                        onToggled: extRoot.revealKeys = checked
                    }
                }

                TextField {
                    id: baseUrlField
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    font.pixelSize: Theme.bodySmallSize
                    placeholderText: "http://localhost:8080"
                    selectByMouse: true
                    onTextChanged: {
                        if (extRoot.loading)
                            return
                        // The answer of the last check belongs to the
                        // address of that time.
                        ConnectionProbe.reset()
                        extRoot.edited()
                    }
                }
                SecretKeyField {
                    id: sharedKey
                    Layout.fillWidth: true
                    keySet: extRoot.sharedKeyPresent
                    revealed: extRoot.revealKeys
                    commit: function(text) { Settings.setApiKey(text) }
                }
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

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                LLOLabel {
                    Layout.fillWidth: true
                    font.pixelSize: Theme.footnoteSize
                    color: Theme.helpColor
                    wrapMode: Text.WordWrap
                    text: extRoot.storeNote()
                }

                Rectangle {
                    visible: extRoot.storeError().length > 0
                    implicitWidth: storeErrorRow.implicitWidth + Theme.spacing * 2
                    implicitHeight: storeErrorRow.implicitHeight + Theme.spacingSmall * 2
                    radius: Theme.controlRadius
                    color: Theme.warningBg
                    border.color: Theme.warning
                    border.width: 1

                    RowLayout {
                        id: storeErrorRow
                        anchors.centerIn: parent
                        spacing: Theme.spacingSmall

                        Icon {
                            name: "info"
                            size: Theme.iconSizeSmall
                            color: Theme.accentAttention
                        }
                        LLOLabel {
                            text: extRoot.storeError()
                            font.pixelSize: Theme.footnoteSize
                            color: Theme.textPrimary
                        }
                        LLOButton {
                            text: qsTr("Retry")
                            flat: true
                            onClicked: Settings.refreshSecretState()
                        }
                    }
                }
            }

            LLOLabel {
                Layout.fillWidth: true
                visible: extRoot.storeError().length > 0 && Settings.secretError.length > 0
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.footnoteSize
                color: Theme.helpColor
                text: qsTr("The system store did not answer: %1").arg(Settings.secretError)
            }
        }
    }

    // The tasks are a card of their own: the shared server above them is one
    // thing, the models below it are another.
    Rectangle {
        id: tasksCard

        visible: !extRoot.compact
        Layout.fillWidth: true
        implicitHeight: tasksColumn.implicitHeight + Theme.spacing * 2
        color: Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1
        radius: Theme.controlRadius + 2

        ColumnLayout {
            id: tasksColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing
            spacing: Theme.spacingSmall

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                LLOLabel {
                    text: qsTr("Tasks")
                    font.pixelSize: Theme.bodySize
                    font.bold: true
                    color: Theme.textPrimary
                }
                Item { Layout.fillWidth: true }
                LLOLabel {
                    text: qsTr("An empty field takes the shared value")
                    font.pixelSize: Theme.footnoteSize
                    color: Theme.helpColor
                }
            }

            Repeater {
                id: taskRows
                model: extRoot.compact ? [] : extRoot.taskIds
                delegate: TaskConnectionRow {
                    required property var modelData

                    task: modelData
                    // The row is as wide as the block it stands in: the
                    // repeater's items are the rows of the block.
                    Layout.fillWidth: true
                    titleWidth: extRoot.titleWidth
                    revealed: extRoot.revealKeys
                    reloadToken: extRoot.reloadToken
                    commitToken: extRoot.commitToken
                    onEdited: extRoot.edited()
                }
            }
        }
    }
}
