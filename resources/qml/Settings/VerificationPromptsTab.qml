pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    signal edited()

    property int currentIndex: -1
    property bool showFormatHelp: false

    readonly property bool hasSelection: currentIndex >= 0
    readonly property int modelRevision: Verification.blockModel.revision
    readonly property bool modifiedFromOriginal: hasSelection
        && promptArea.text !== Verification.originalPromptAt(currentIndex)
    readonly property bool userModified: hasSelection && modelRevision >= 0
        && Verification.blockModel.promptAt(currentIndex)
           !== Verification.originalPromptAt(currentIndex)
    readonly property bool draftDirty: hasSelection
        && promptArea.text !== Verification.blockModel.promptAt(currentIndex)

    function selectIndex(index) {
        if (index === root.currentIndex)
            return
        if (root.currentIndex >= 0)
            savePrompt()
        root.currentIndex = index
        typeList.currentIndex = index
        loadPrompt()
    }

    function savePrompt() {
        if (root.currentIndex < 0)
            return
        if (promptArea.text === Verification.blockModel.promptAt(root.currentIndex))
            return
        Verification.blockModel.setPrompt(root.currentIndex, promptArea.text)
        root.edited()
    }

    function loadPrompt() {
        promptArea.text = root.hasSelection
            ? Verification.blockModel.promptAt(root.currentIndex) : ""
    }

    function revertCurrentPrompt() {
        if (!root.hasSelection)
            return
        const original = Verification.originalPromptAt(root.currentIndex)
        Verification.blockModel.setPrompt(root.currentIndex, original)
        promptArea.text = original
        root.edited()
    }

    Connections {
        target: Verification
        function onModelChanged() {
            root.currentIndex = -1
            typeList.currentIndex = -1
            promptArea.text = ""
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        spacing: Theme.spacingLarge

        handle: Rectangle {
            implicitWidth: 20
            color: "transparent"

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 1
                height: parent.height
                color: Theme.divider
            }
        }

        // --- Type list ----------------------------------------------------
        ListView {
            id: typeList
            SplitView.preferredWidth: 260
            SplitView.minimumWidth: 180
            SplitView.maximumWidth: 360
            clip: true
            spacing: 0
            model: Verification.blockModel
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            section.property: "group"

            section.delegate: Item {
                id: sectionRow
                required property string section
                readonly property bool first: section === "content"

                width: typeList.width
                height: sectionLabel.implicitHeight + 8
                        + (first ? 0 : Theme.spacingLarge)

                LLOLabel {
                    id: sectionLabel
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing
                    anchors.top: parent.top
                    anchors.topMargin: sectionRow.first ? 4 : 4 + Theme.spacingLarge
                    text: BlockNames.groupTitle(sectionRow.section)
                    font.pointSize: Theme.footnoteSize
                    font.bold: true
                    font.capitalization: Font.AllUppercase
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                }
            }

            delegate: Item {
                id: typeRow

                required property int index
                required property string type
                required property string name
                required property bool enabled

                width: typeList.width
                implicitHeight: Math.max(Theme.rowHeightLarge,
                                         nameLabel.implicitHeight + 10)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.controlRadius
                    color: typeList.currentIndex === typeRow.index
                               ? Theme.selected
                               : (rowHover.hovered ? Theme.surfaceSunken
                                                   : "transparent")
                }

                LLOLabel {
                    id: nameLabel
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing
                    anchors.right: modifiedDot.visible ? modifiedDot.left
                                                       : parent.right
                    anchors.rightMargin: Theme.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    text: BlockNames.displayName(typeRow.name, typeRow.type)
                    wrapMode: Text.WordWrap
                    color: typeList.currentIndex === typeRow.index
                               ? Theme.textPrimary : Theme.textSecondary
                }

                Rectangle {
                    id: modifiedDot
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6
                    height: 6
                    radius: 3
                    visible: Verification.blockModel.promptAt(typeRow.index)
                             !== Verification.originalPromptAt(typeRow.index)
                    color: Theme.warning
                }

                HoverHandler { id: rowHover }

                TapHandler {
                    onTapped: root.selectIndex(typeRow.index)
                }

                ToolTip.visible: rowHover.hovered && modifiedDot.visible
                ToolTip.delay: 600
                ToolTip.text: qsTr("Prompt differs from the built-in one")
            }
        }

        // --- Editor -------------------------------------------------------
        ColumnLayout {
            id: editorPane
            SplitView.fillWidth: true
            spacing: Theme.spacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                LLOLabel {
                    text: root.hasSelection
                        ? BlockNames.displayName(
                              Verification.blockModel.nameAt(root.currentIndex),
                              Verification.blockModel.typeAt(root.currentIndex))
                        : qsTr("Select a block type")
                    font.bold: true
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
                LLOLabel {
                    visible: root.hasSelection
                    text: root.hasSelection
                        ? Verification.blockModel.typeAt(root.currentIndex) : ""
                    font.pointSize: Theme.footnoteSize
                    color: Theme.helpColor
                }
                Item { Layout.fillWidth: true }
            }

            LLOLabel {
                visible: root.hasSelection
                font.pointSize: Theme.footnoteSize
                color: Theme.helpColor
                text: Verification.blockModel.enabledAt(root.currentIndex)
                    ? qsTr("Selected for checking")
                    : qsTr("Not selected for checking")
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: formatLabel.implicitHeight
                visible: root.hasSelection

                TapHandler {
                    onTapped: root.showFormatHelp = !root.showFormatHelp
                }
                HoverHandler { id: formatHover }

                LLOLabel {
                    id: formatLabel
                    text: (root.showFormatHelp ? "\u25be " : "\u25b8 ")
                          + qsTr("Answer format: OK / FIX / REVIEW")
                    font.pointSize: Theme.footnoteSize
                    color: formatHover.hovered ? Theme.textPrimary : Theme.linkColor
                }
            }
            LLOLabel {
                Layout.fillWidth: true
                visible: root.hasSelection && root.showFormatHelp
                font.pointSize: Theme.footnoteSize
                color: Theme.helpColor
                wrapMode: Text.WordWrap
                text: qsTr("OK — the block matches the image. FIX — the block was "
                           + "corrected: on the next line output the complete "
                           + "corrected block, it replaces the original. "
                           + "REVIEW — the block is unreadable, cropped or ambiguous.")
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSmall
                visible: root.hasSelection
                spacing: Theme.spacing

                LLOLabel {
                    text: qsTr("Prompt")
                    font.bold: true
                    color: Theme.textPrimary
                }
                Item {
                    Layout.fillWidth: true
                }

                LLOButton {
                    visible: root.modifiedFromOriginal
                    subtle: true
                    text: qsTr("Restore original prompt")
                    onClicked: revertConfirmDialog.open()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.hasSelection
                radius: Theme.dialogRadius
                color: Theme.surfaceAlt
                border.color: promptArea.activeFocus ? Theme.accent : Theme.divider
                border.width: promptArea.activeFocus ? 2 : 1

                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth

                    TextArea {
                        id: promptArea
                        width: parent.width
                        padding: 16
                        enabled: root.hasSelection
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        textFormat: Text.PlainText
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        placeholderText: qsTr("Prompt for the selected block type")
                        font.pointSize: Theme.bodySmallSize
                        onEditingFinished: root.savePrompt()
                    }
                }
            }

            Item { Layout.fillHeight: true; visible: !root.hasSelection }
        }
    }

    Dialog {
        id: revertConfirmDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 400
        title: qsTr("Restore original prompt?")

        LLOLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            text: qsTr("The customized prompt for this block type will be "
                       + "replaced by the built-in one. The change is applied "
                       + "to the window draft and is stored only after Save.")
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.revertCurrentPrompt()
    }
}
