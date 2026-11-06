pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    signal edited()

    property bool loading: false

    // The thinking budget is a level, not a token count: the row of buttons
    // picks the level, the level maps onto the tokens the model may spend.
    property int fixReasoningIndex: 0
    property int readReasoningIndex: 0

    readonly property var reasoningBudgetSteps: [0, 128, 256, 512]
    readonly property var reasoningOptions: [
        { label: qsTr("Off") },
        { label: qsTr("Low") },
        { label: qsTr("Medium") },
        { label: qsTr("High") }
    ]

    function reasoningIndexFor(tokens) {
        const index = root.reasoningBudgetSteps.indexOf(tokens)
        return index >= 0 ? index : 0
    }

    function loadValues() {
        loading = true
        showConfidenceCheckBox.checked = Settings.showConfidence
        root.fixReasoningIndex = root.reasoningIndexFor(Settings.fixReasoningBudgetTokens)
        root.readReasoningIndex = root.reasoningIndexFor(Settings.readReasoningBudgetTokens)
        loading = false
    }

    function saveValues() {
        Settings.showConfidence = showConfidenceCheckBox.checked
        Settings.fixReasoningBudgetTokens = root.reasoningBudgetSteps[root.fixReasoningIndex]
        Settings.readReasoningBudgetTokens = root.reasoningBudgetSteps[root.readReasoningIndex]
    }

    function resetValues() {
        // The zeros write through at once (like Verification.resetToDefaults()):
        // Cancel after Reset must not resurrect the old values from settings.
        Settings.fixReasoningBudgetTokens = 0
        Settings.readReasoningBudgetTokens = 0
        root.fixReasoningIndex = 0
        root.readReasoningIndex = 0
    }

    component BlockTypeRow: Item {
        id: typeRow

        // Note: the Repeater model is a group-filtered proxy, so there is no
        // `required property int index` here — the source-model row is
        // resolved from the unique `type` (see the TapHandler below). The
        // check-state role arrives as `checkEnabled`, not `enabled`: a
        // required `enabled` would shadow Item::enabled and disable the whole
        // row exactly when its type is unchecked.
        required property string type
        required property string name
        required property bool checkEnabled

        Layout.fillWidth: true
        Layout.preferredHeight: Theme.rowHeightCompact

        Rectangle {
            anchors.fill: parent
            radius: Theme.controlRadius
            color: rowHover.hovered ? Theme.selected : "transparent"
        }

        Rectangle {
            id: box
            x: 4
            anchors.top: parent.top
            anchors.topMargin: (Theme.rowHeightCompact - 16) / 2
            width: 16
            height: 16
            radius: Theme.controlRadius
            color: typeRow.checkEnabled ? Theme.accent : Theme.surface
            border.color: typeRow.checkEnabled ? Theme.accent : Theme.border
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "\u2713"
                visible: typeRow.checkEnabled
                font.pointSize: Theme.footnoteSize
                font.bold: true
                color: Theme.dark ? "#1c1c1c" : "#ffffff"
            }
        }

        LLOLabel {
            id: nameLabel
            anchors.left: box.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSmall
            anchors.top: parent.top
            anchors.topMargin: (Theme.rowHeightCompact - nameLabel.implicitHeight) / 2
            text: BlockNames.displayName(typeRow.name, typeRow.type)
            font.pointSize: Theme.bodySmallSize
            color: Theme.textPrimary
            elide: Text.ElideRight
        }

        HoverHandler { id: rowHover }
        TapHandler {
            onTapped: {
                // The Repeater model is a group-filtered proxy, so `index`
                // addresses the proxy; resolve the source-model row by the
                // unique block type instead.
                const sourceRow = Verification.blockModel.rowOfType(typeRow.type)
                if (sourceRow >= 0)
                    Verification.blockModel.setEnabled(sourceRow, !typeRow.checkEnabled)
            }
        }

        ToolTip.visible: rowHover.hovered && typeRow.type.length > 0
        ToolTip.delay: 600
        ToolTip.text: typeRow.type
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSmall
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Thinking during fix")
                font.pointSize: Theme.bodySmallSize
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }
            SegmentedToggle {
                currentIndex: root.fixReasoningIndex
                options: root.reasoningOptions
                onActivated: (idx) => {
                    root.fixReasoningIndex = idx
                    root.edited()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSmall
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Thinking during read")
                font.pointSize: Theme.bodySmallSize
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }
            SegmentedToggle {
                currentIndex: root.readReasoningIndex
                options: root.reasoningOptions
                onActivated: (idx) => {
                    root.readReasoningIndex = idx
                    root.edited()
                }
            }
        }
        LLOLabel {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSmall
            font.pointSize: Theme.footnoteSize
            color: Theme.helpColor
            wrapMode: Text.WordWrap
            text: qsTr("Tokens the model may spend thinking before the answer. "
                       + "Models without a thinking channel ignore this; when the "
                       + "context window cannot fit the thinking, the budget is cut "
                       + "and the status line says so.")
        }

        LLOCheckBox {
            id: showConfidenceCheckBox
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing
            text: qsTr("Show model confidence")
            font.pointSize: Theme.bodySmallSize
            onToggled: if (!loading) root.edited()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingLarge
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Block types to check")
                font.pointSize: Theme.bodySize
                color: Theme.textPrimary
            }
            LLOLabel {
                text: qsTr("Select all")
                font.pointSize: Theme.footnoteSize
                color: selAllHover.hovered ? Theme.textPrimary : Theme.linkColor
                TapHandler {
                    onTapped: Verification.blockModel.setAllEnabled(true)
                }
                HoverHandler {
                    id: selAllHover
                    cursorShape: Qt.PointingHandCursor
                }
            }
            LLOLabel {
                text: "\u00b7"
                font.pointSize: Theme.footnoteSize
                color: Theme.helpColor
            }
            LLOLabel {
                text: qsTr("Deselect all")
                font.pointSize: Theme.footnoteSize
                color: selNoneHover.hovered ? Theme.textPrimary : Theme.linkColor
                TapHandler {
                    onTapped: Verification.blockModel.setAllEnabled(false)
                }
                HoverHandler {
                    id: selNoneHover
                    cursorShape: Qt.PointingHandCursor
                }
            }
            Item { Layout.fillWidth: true }
            LLOLabel {
                font.pointSize: Theme.footnoteSize
                color: Theme.helpColor
                text: qsTr("Selected %1 of %2")
                      .arg(Verification.blockModel.enabledCount)
                      .arg(Verification.blockModel.totalCount)
            }
        }

        ScrollView {
            id: typesScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spacing
            contentWidth: availableWidth
            // The content is at least the tallest card, and at least the
            // viewport: a language with taller lines must not clip the last
            // row of the tallest group, and a shorter content must not leave
            // a gap under the cards.
            contentHeight: Math.max(columnsRow.implicitHeight, typesScroll.availableHeight)

            RowLayout {
                id: columnsRow
                width: typesScroll.availableWidth
                height: Math.max(implicitHeight, typesScroll.availableHeight)
                spacing: Theme.spacing

                Repeater {
                    model: [
                        { group: "content", source: Verification.blockModelContent },
                        { group: "captions", source: Verification.blockModelCaptions },
                        { group: "service", source: Verification.blockModelService }
                    ]
                    delegate: Rectangle {
                        id: groupCard
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredWidth: columnsRow.width / 3
                        Layout.minimumWidth: 0
                        Layout.fillHeight: true
                        Layout.preferredHeight: cardColumn.implicitHeight + Theme.spacing * 2

                        color: Theme.surfaceAlt
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.controlRadius + 2

                        // The column takes its natural height and hangs from the
                        // top of the card: the rows keep their own distance,
                        // and the card that is taller than its list leaves the
                        // slack at the bottom instead of spreading the rows.
                        ColumnLayout {
                            id: cardColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.spacing
                            spacing: Theme.spacingSmall

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                LLOLabel {
                                    Layout.fillWidth: true
                                    text: BlockNames.groupTitle(groupCard.modelData.group)
                                    font.pointSize: Theme.bodySmallSize
                                    font.bold: true
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                }
                                LLOLabel {
                                    text: {
                                        const counts = Verification.blockGroupCounts[groupCard.modelData.group]
                                        return counts ? counts.enabled + "/" + counts.total : ""
                                    }
                                    font.pointSize: Theme.footnoteSize
                                    color: Theme.helpColor
                                }
                            }

                            Repeater {
                                model: groupCard.modelData.source
                                delegate: BlockTypeRow {}
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing
            spacing: Theme.spacingSmall

            Icon {
                name: "info"
                size: 14
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                Layout.alignment: Qt.AlignVCenter
                color: Theme.helpColor
                Accessible.ignored: true
            }
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.footnoteSize
                color: Theme.helpColor
                wrapMode: Text.WordWrap
                text: qsTr("Blocks without OCR text are not checked.")
            }
        }
    }
}
