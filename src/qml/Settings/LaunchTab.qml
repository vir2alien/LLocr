pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property string role: "pageOcr"
    property var runtimeSettingsRef: null

    readonly property bool externalMode: Settings.connectionMode === "external"

    readonly property var profiles: LaunchProfiles

    readonly property string runtimeNote: RoleProfiles.runtimeNote(role)

    property var memoryEstimate: ({})

    function loadValues() {
        root.profiles.reloadDraft()
        root.memoryEstimate = root.externalMode ? ({}) : Runtime.estimateModelMemory(Settings.launchModelPathForRole(root.role), root.role)
    }

    function saveValues() {
        root.profiles.saveDraft()
    }

    function resetValues() {
        root.profiles.loadDefaultDraft()
    }

    readonly property real nameWidth: 0.24
    readonly property real valueWidth: 0.24

    ColumnLayout {
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
            setValue: (row, text) => root.profiles.setDraftValue(row, text)
        }

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
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
            font.pointSize: Theme.captionSize
            color: Theme.error
            visible: root.memoryEstimate.overVramBudget === true
            text: qsTr("The model does not fit in this card's video memory. Lower the number of GPU layers or the context window, or pick a smaller quantization.")
        }

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("The machine's parameters: how much of the model goes to the GPU, flash attention, and the context window. Everything else — the server policy and the model's own parameters — is tuned by the app and its model profiles.")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: root.externalMode
        spacing: 6

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("The model is managed by the external server. Location and download settings are not available in this mode.")
        }

        LLOButton {
            text: qsTr("Configure runtime…")
            onClicked: {
                if (root.runtimeSettingsRef)
                    root.runtimeSettingsRef.bringToFront()
            }
        }

        Item { Layout.fillHeight: true }
    }
}
