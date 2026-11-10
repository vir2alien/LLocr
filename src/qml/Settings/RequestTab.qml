pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    // Model task id: "blockRecognition", "decision" or "layout".
    property string role: "blockRecognition"
    readonly property bool decisionRole: role === "decision"
    readonly property bool hasParams: profiles.draftModel.count > 0
    readonly property real nameWidth: 0.2
    readonly property real valueWidth: 0.2
    readonly property var profiles: RoleProfiles.profiles(role)

    // The user changed a request parameter.
    signal edited()

    // The load and the reset write the threshold themselves: those are the
    // app's own writes, not the user's edit.
    property bool writingThreshold: false

    // The height of the tab's content: the popup that holds the tabs sizes
    // itself by the taller of them.
    readonly property real contentHeight: requestColumn.implicitHeight

    function loadValues() {
        root.profiles.reloadDraft()
        root.setThreshold(Math.round(Settings.decisionMatchThreshold * 100))
    }

    function saveValues() {
        root.profiles.saveDraft()
        if (root.decisionRole)
            Settings.decisionMatchThreshold = thresholdSpin.value / 100
        return true
    }

    function resetValues() {
        root.profiles.loadDefaultDraft()
        if (root.decisionRole)
            root.setThreshold(Math.round(Settings.defaultDecisionMatchThreshold * 100))
    }

    function setThreshold(value) {
        root.writingThreshold = true
        thresholdSpin.value = value
        root.writingThreshold = false
    }

    ColumnLayout {
        id: requestColumn
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Item { implicitHeight: 4 }

        RowLayout {
            visible: root.decisionRole
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Match threshold (%)")
                font.pixelSize: Theme.bodySmallSize
            }
            SpinBox {
                id: thresholdSpin
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.bodySmallSize
                from: 0
                to: 100
                stepSize: 5
                editable: true
                // The threshold is written on save: the box reports a change
                // of the user's, not the app's own write.
                onValueChanged: {
                    if (!root.writingThreshold)
                        root.edited()
                }
            }
            Item { Layout.fillWidth: true }
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: root.decisionRole
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.footnoteSize
            color: Theme.helpColor
            text: qsTr("The decision model answers with the probability that the text matches the image. A block counts as correct when the probability reaches this threshold; the rest are re-recognized when automatic re-recognition is on.")
        }

        ParamsTableEditor {
            visible: root.hasParams
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.profiles.draftModel
            nameWidth: root.nameWidth
            valueWidth: root.valueWidth
            setValue: (row, text) => {
                const written = root.profiles.setDraftValue(row, text)
                if (written)
                    root.edited()
                return written
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: root.decisionRole && !root.hasParams
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.footnoteSize
            color: Theme.helpColor
            text: qsTr("The decision model answers in a single forward pass and generates no text, so it has no sampling parameters — the threshold above is its only setting here.")
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: root.hasParams
            font.pixelSize: Theme.footnoteSize
            color: Theme.helpColor
            wrapMode: Text.WordWrap
            text: RoleProfiles.samplingHint(role)
        }

        // Without the parameter table there is nothing to absorb the free
        // height, and the layout stretches the remaining rows across the whole
        // column; the spring keeps them packed at the top.
        Item {
            visible: !root.hasParams
            Layout.fillHeight: true
        }
    }
}
