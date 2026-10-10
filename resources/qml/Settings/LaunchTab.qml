pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property string role: "ocr"
    property var runtimeSettingsRef: null

    readonly property bool externalMode: Settings.connectionMode === "external"

    readonly property var profiles: LaunchProfiles

    readonly property string runtimeNote: role === "blockRecognition" ? profiles.checkModelRuntimeNote
                                        : role === "decision" ? profiles.decisionModelRuntimeNote
                                        : profiles.modelRuntimeNote

    function loadValues() {
        root.profiles.reloadDraft()
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
