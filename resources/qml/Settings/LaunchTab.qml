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

    readonly property bool modelProfileMissing: role === "check"
        ? profiles.checkModelProfileMissing
        : profiles.modelProfileMissing

    readonly property string runtimeNote: role === "check"
        ? profiles.checkModelRuntimeNote
        : profiles.modelRuntimeNote

    function loadValues() {
        root.profiles.reloadDraft(root.role)
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

        LLOLabel {
            Layout.fillWidth: true
            visible: root.modelProfileMissing
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.warning
            text: qsTr("This model is not in the catalog, so its launch parameters come from the fallback set instead of the model's own. The context window may be wrong for it — check the log if a page comes out truncated.")
        }

        RuntimeNoteWarning {
            note: root.runtimeNote
        }

        ParamsTableEditor {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.profiles.draftModel
            nameWidth: root.nameWidth
            valueWidth: root.valueWidth
            removable: true
            valuePlaceholder: qsTr("(flag)")
            setValue: (row, text) => root.profiles.setDraftValue(row, text)
            addRow: (name, value) => root.profiles.appendDraftParameter(name, value)
            removeRow: (row) => root.profiles.removeDraftRow(row)
        }

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("llama-server command-line parameters; --model/--mmproj/--alias/--host/--port come from the other launch settings. The greyed-out rows belong to a layer: the shared server policy, this machine's build, or the model itself — they are what the server is started with, and the profile owns them.")
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
                    root.runtimeSettingsRef.show()
            }
        }

        Item { Layout.fillHeight: true }
    }
}
