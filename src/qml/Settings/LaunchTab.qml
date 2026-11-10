pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property string role: "blockRecognition"
    // The table that hosts this tab: it relays the request for the runtime
    // settings up to the window.
    property var hostTable: null

    // The host can show the runtime settings. Where it cannot (the setup
    // wizard), the button would lead nowhere and stays hidden.
    property bool runtimeSectionAvailable: true

    // The question about a secret-like value belongs to the draft: the answer
    // holds until a new value is written into it.
    property bool secretConfirmed: false

    // The user changed a launch parameter.
    signal edited()

    readonly property bool externalMode: Settings.connectionMode === "external"

    readonly property var profiles: LaunchProfiles

    readonly property string runtimeNote: RoleProfiles.runtimeNote(role)

    // The height of the tab's content: the popup that holds the tabs sizes
    // itself by the taller of them.
    readonly property real contentHeight: launchColumn.implicitHeight

    property var memoryEstimate: ({})

    function loadValues() {
        root.profiles.reloadDraft()
        root.memoryEstimate = root.externalMode ? ({}) : Runtime.estimateModelMemory(Settings.launchModelPathForRole(root.role), root.role)
    }

    // False when the draft was not written: a secret-like parameter asks the
    // user first, and the caller must not go on as if the values were saved.
    // A confirmed draft is written without the question again.
    function saveValues() {
        if (!root.secretConfirmed && root.profiles.draftHasSecretLikeParameter()) {
            secretValueDialog.open()
            return false
        }
        root.profiles.saveDraft()
        return true
    }

    function resetValues() {
        root.secretConfirmed = false
        root.profiles.loadDefaultDraft()
    }

    readonly property real nameWidth: 0.2
    readonly property real valueWidth: 0.2

    ColumnLayout {
        id: launchColumn
        anchors.fill: parent
        visible: !root.externalMode
        spacing: Theme.spacingSmall

        Item { implicitHeight: 4 }

        RuntimeNoteWarning {
            note: root.runtimeNote
        }

        ParamsTableEditor {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.profiles.draftModel
            nameWidth: root.nameWidth
            valueWidth: root.valueWidth
            valuePlaceholder: qsTr("(flag)")
            setValue: (row, text) => {
                const written = root.profiles.setDraftValue(row, text)
                if (written) {
                    root.secretConfirmed = false
                    root.edited()
                }
                return written
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.footnoteSize
            color: Theme.helpColor
            visible: root.memoryEstimate.valid === true
            text: {
                // The estimate map is a placeholder until the popup opens, and
                // a model outside the catalog has no profile: the encoder's
                // cost is unknown and stays out of the sentence.
                if (root.memoryEstimate.valid !== true)
                    return ""
                if (root.memoryEstimate.visionLabel.length === 0)
                    return qsTr("Video memory for this model: %1 of %2 on this card — weights %3, projector %4, draft %5, KV cache %6, working set %7, overhead %8.")
                            .arg(root.memoryEstimate.requiredLabel)
                            .arg(root.memoryEstimate.vramLabel)
                            .arg(root.memoryEstimate.modelLabel)
                            .arg(root.memoryEstimate.mmprojLabel)
                            .arg(root.memoryEstimate.draftLabel)
                            .arg(root.memoryEstimate.kvLabel)
                            .arg(root.memoryEstimate.computeLabel)
                            .arg(root.memoryEstimate.overheadLabel)
                return qsTr("Video memory for this model: %1 of %2 on this card — weights %3, projector %4, draft %5, KV cache %6, working set %7, vision %8, overhead %9.")
                        .arg(root.memoryEstimate.requiredLabel)
                        .arg(root.memoryEstimate.vramLabel)
                        .arg(root.memoryEstimate.modelLabel)
                        .arg(root.memoryEstimate.mmprojLabel)
                        .arg(root.memoryEstimate.draftLabel)
                        .arg(root.memoryEstimate.kvLabel)
                        .arg(root.memoryEstimate.computeLabel)
                        .arg(root.memoryEstimate.visionLabel)
                        .arg(root.memoryEstimate.overheadLabel)
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.footnoteSize
            color: Theme.error
            visible: root.memoryEstimate.overVramBudget === true
            text: qsTr("The model does not fit in this card's video memory. Lower the number of GPU layers or the context window, or pick a smaller quantization.")
        }

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.footnoteSize
            color: Theme.helpColor
            text: qsTr("The machine's parameters: how much of the model goes to the GPU, flash attention, and the context window. Everything else — the server policy and the model's own parameters — is tuned by the app and its model profiles.")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: root.externalMode
        spacing: Theme.spacingSmall

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.footnoteSize
            color: Theme.helpColor
            text: qsTr("The model is managed by the external server. Location and download settings are not available in this mode.")
        }

        LLOButton {
            text: qsTr("Configure runtime…")
            visible: root.runtimeSectionAvailable
            onClicked: {
                if (root.hostTable)
                    root.hostTable.runtimeSectionRequested()
            }
        }

        Item { Layout.fillHeight: true }
    }

    // The platform rows carry no secrets, but a row whose name reads like one
    // (api-key, a token, a password) would persist its value as plain text in
    // the user's JSON profile — the save asks first.
    InterruptConfirmDialog {
        id: secretValueDialog

        title: qsTr("Save the value anyway?")
        prompt: qsTr("The value will be stored as plain text in the launch profile.")
        promptColor: Theme.accentAttention
        onConfirmed: {
            root.profiles.saveDraft()
            root.secretConfirmed = true
        }
    }
}
