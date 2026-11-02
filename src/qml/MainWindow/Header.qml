pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

ToolBar {
    id: headerRoot

    signal openFileRequested()
    signal openProjectRequested()
    signal saveProjectRequested()
    signal saveProjectAsRequested()
    // The ExportDialog's preset scope: 0 = whole document, 1 = current page.
    signal exportRequested(int presetScope)
    signal setupWizardRequested()
    signal openUiSettingsRequested()
    signal openOutputSettingsRequested()
    signal openVerificationSettingsRequested()
    signal openRuntimeSettingsRequested()
    signal openTaskModelsSettingsRequested()

    leftPadding: Theme.spacing
    rightPadding: Theme.spacing

    // Fusion's MenuItem draws no shortcut text, so each item with a shortcut
    // carries its own caption; on macOS the stored "Ctrl+" is really Cmd.
    function shortcutDisplayText(sequence) {
        if (Qt.platform.os !== "osx")
            return sequence
        return String(sequence).replace("Ctrl+Shift+", "⇧⌘").replace("Ctrl+", "⌘")
    }

    // The page count the pipeline acts on: an empty selection means the whole
    // document; the «Целевые страницы» field is the same selection written as
    // a range.
    readonly property int scopePageCount: Controller.selectedPageCount > 0
                                          ? Controller.selectedPageCount
                                          : Controller.pageCount

    // The pipeline buttons' idle gate: recognition machinery plus the model
    // download queue (the download queue has no toolbar affordance of its own
    // — it is cancelled where it starts, in the Models window or the wizard).
    readonly property bool pipelineIdle: Controller.actionsIdle
                                         && !ModelInstaller.taskQueueActive

    implicitHeight: headerRow.implicitHeight + topPadding + bottomPadding

    background: Rectangle {
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.divider
        }
    }

    RowLayout {
        id: headerRow
        anchors.fill: parent
        spacing: Theme.spacingSmall

        ToolGroup {
            title: qsTr("Document")

            RowLayout {
                spacing: 4

                IconToolButton {
                    id: fileButton
                    text: qsTr("File")
                    iconName: "file-text"
                    onClicked: fileMenu.popup(fileButton, 0, fileButton.height + 2)
                }

                IconToolButton {
                    id: importButton
                    text: qsTr("Import")
                    iconName: "image"
                    enabled: !Controller.busy && !Controller.importing && !Controller.exporting
                             && !Controller.checkBusy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open images, PDF or DjVu")
                    onClicked: headerRoot.openFileRequested()
                }

                IconToolButton {
                    text: qsTr("Export")
                    iconName: "upload"
                    enabled: Controller.hasResult && !Controller.exporting && !Controller.importing
                             && !Controller.checkBusy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Export the recognized text")
                    onClicked: headerRoot.exportRequested(0)
                }
            }
        }

        ToolDivider {}

        ToolGroup {
            title: qsTr("Target pages")

            RowLayout {
                spacing: 4

                TextField {
                    id: pagesField

                    implicitWidth: 120
                    implicitHeight: 28
                    // The Fusion field pads the text by 4 px above and below, and
                    // the system font's line is 21 px on macOS: 26 px of height
                    // needs 2 px of padding, or the text is cut.
                    topPadding: 2
                    bottomPadding: 2
                    // The text starts where the prefix and the buttons'
                    // captions start.
                    leftPadding: 5 + pagesPrefix.width + 4
                    rightPadding: 5
                    enabled: Controller.hasImage
                    placeholderText: qsTr("All")
                    ToolTip.visible: hovered && enabled
                    ToolTip.text: qsTr("Pages to process: follows the selection in the thumbnail panel, or type a range like 1-10, 15")

                    // The prefix names the field: a bare number would be read
                    // as a page of the document, not as the scope of the run.
                    Text {
                        id: pagesPrefix
                        anchors.left: parent.left
                        anchors.leftMargin: 5
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Pages:")
                        font: pagesField.font
                        color: Theme.textMuted
                    }

                    function applyRange() {
                        Controller.pageModel.applyPagesRange(pagesField.text)
                        pagesField.text = Controller.pageModel.selectedPagesRange()
                    }

                    onAccepted: applyRange()
                    onActiveFocusChanged: {
                        if (activeFocus)
                            selectAll()
                        else
                            applyRange()
                    }

                    Connections {
                        target: Controller.pageModel
                        function onSelectionChanged() {
                            if (!pagesField.activeFocus)
                                pagesField.text = Controller.pageModel.selectedPagesRange()
                        }
                    }
                }

                // The button can be off while its reason must still be readable: a
                // disabled control gets no hover events, so the pointer handler
                // lives on a wrapper that stays enabled.
                Item {
                    id: autoButtonArea

                    implicitWidth: autoButton.implicitWidth
                    implicitHeight: autoButton.implicitHeight
                    Layout.minimumWidth: autoButton.implicitWidth

                    HoverHandler {
                        id: autoAreaHover
                    }

                    IconToolButton {
                        id: autoButton

                        anchors.fill: parent
                        text: qsTr("Auto pass")
                        iconName: "zap"
                        stopMode: Controller.autoPipelineRunning
                        enabled: stopMode || (headerRoot.pipelineIdle && Controller.hasImage
                                 && Controller.autoPipelineAvailable)
                        ToolTip.visible: hovered || autoAreaHover.hovered
                        ToolTip.text: {
                            // The C++ owns the reason and the wording; the button only
                            // shows it. One read: the reason scans the scope.
                            const reason = Controller.autoPipelineBlockedReason
                            return stopMode ? qsTr("Stop the automatic pipeline")
                                            : (reason.length > 0
                                               ? reason
                                               : qsTr("Mark up, recognize, verify and fix %1 pages").arg(headerRoot.scopePageCount))
                        }
                        onClicked: {
                            if (stopMode) {
                                Controller.stopAutoPipeline()
                                return
                            }
                            autoConfirmDialog.requestAutoPipeline()
                        }
                    }
                }
            }
        }

        ToolDivider {}

        ToolGroup {
            title: qsTr("Markup and OCR")

            RowLayout {
                spacing: 4

                IconToolButton {
                    text: qsTr("Mark up")
                    iconName: "layout-template"
                    stopMode: Controller.layoutBusy
                    enabled: stopMode || (headerRoot.pipelineIdle && Controller.hasImage
                             && Controller.layoutAvailable && Controller.layoutModelAvailable)
                    ToolTip.visible: hovered
                    ToolTip.text: stopMode ? qsTr("Stop the layout pass")
                                           : qsTr("Find the blocks on the selected pages")
                    onClicked: {
                        if (stopMode) {
                            Controller.stopLayout()
                            return
                        }
                        markupConfirmDialog.requestMarkup()
                    }
                }

                IconToolButton {
                    text: qsTr("Recognize blocks")
                    iconName: "scan-text"
                    stopMode: Controller.checkBusy && Controller.checkRecheckPhase
                    enabled: stopMode || (headerRoot.pipelineIdle && Controller.hasImage
                             && Controller.scopeBlocksRecognitionSupported
                             && Controller.blockRecognitionModelAvailable)
                    ToolTip.visible: hovered
                    ToolTip.text: stopMode ? qsTr("Stop the block recognition")
                                           : qsTr("Recognize the marked blocks that still have no text")
                    onClicked: {
                        if (stopMode) {
                            Controller.stopCheck()
                            return
                        }
                        Controller.recognizeScopeBlocks()
                    }
                }
            }
        }

        ToolDivider {}

        ToolGroup {
            title: qsTr("Verification and AI correction")

            RowLayout {
                spacing: 4

                IconToolButton {
                    text: qsTr("Verify")
                    iconName: "circle-check"
                    stopMode: Controller.checkBusy && Controller.checkDecisionPhase
                    enabled: stopMode || (headerRoot.pipelineIdle && Controller.hasImage
                             && Controller.scopeVerificationSupported && Controller.decisionModelAvailable)
                    ToolTip.visible: hovered
                    ToolTip.text: stopMode ? qsTr("Stop the verification")
                                           : qsTr("Verify the recognized blocks without fixing them")
                    onClicked: {
                        if (stopMode) {
                            Controller.stopCheck()
                            return
                        }
                        Controller.checkScopeEnabledBlocks()
                    }
                }

                IconToolButton {
                    text: Controller.scopedPendingFixCount > 0
                          ? qsTr("Fix") + " (" + Controller.scopedPendingFixCount + ")"
                          : qsTr("Fix")
                    iconName: "wand-sparkles"
                    highlight: Controller.scopedPendingFixCount > 0
                    stopMode: Controller.checkBusy && Controller.checkFixPhase
                    enabled: stopMode || (headerRoot.pipelineIdle && Controller.hasImage
                             && Controller.fixModelAvailable && Controller.scopedPendingFixCount > 0)
                    ToolTip.visible: hovered
                    ToolTip.text: stopMode ? qsTr("Stop the fix")
                                           : qsTr("Fix the blocks that wait for a fix on the selected pages")
                    onClicked: {
                        if (stopMode) {
                            Controller.stopCheck()
                            return
                        }
                        Controller.fixScopeProblemBlocks()
                    }
                }

                IconToolButton {
                    text: Controller.scopedReadBlockCount > 0
                          ? qsTr("Re-recognize") + " (" + Controller.scopedReadBlockCount + ")"
                          : qsTr("Re-recognize")
                    iconName: "file-search"
                    stopMode: Controller.checkBusy && Controller.checkReadPhase
                    enabled: stopMode || (headerRoot.pipelineIdle && Controller.hasImage
                             && Controller.fixModelAvailable && Controller.scopedReadBlockCount > 0)
                    ToolTip.visible: hovered
                    ToolTip.text: stopMode ? qsTr("Stop the re-reading")
                                           : qsTr("Read the blocks of the selected pages from the image")
                    onClicked: {
                        if (stopMode) {
                            Controller.stopCheck()
                            return
                        }
                        Controller.readScopeBlocks()
                    }
                }
            }
        }

        ToolDivider {}

        Item { Layout.fillWidth: true }

        ToolGroup {
            captionLine: false

            RowLayout {
                spacing: 4

                IconToolButton {
                    id: settingsButton
                    text: qsTr("Settings")
                    iconName: "settings"
                    onClicked: settingsMenu.popup(settingsButton, 0, settingsButton.height + 2)
                }
            }
        }
    }//RowLayout

    Shortcut {
        sequence: "Ctrl+O"
        enabled: !Controller.busy && !Controller.importing && !Controller.exporting
                 && !Controller.checkBusy
        onActivated: headerRoot.openFileRequested()
    }

    // A toolbar button with a Lucide glyph before its caption; the highlight
    // turns the caption into the warm accent (the «Исправить» badge). In stop
    // mode the caption gives way to the stop glyph inside a spinning ring —
    // the button stays enabled and its click stops the run it started. The
    // implicit width must come from the content: a custom contentItem alone
    // sizes the control to its padding, and the toolbar piles the buttons up.
    // The caption sets that width in both modes, so the button keeps its size
    // when it turns into the stop ring.
    component IconToolButton: ToolButton {
        id: iconButton

        property string iconName: ""
        property int iconSize: 15
        property bool highlight: false
        property bool stopMode: false

        implicitHeight: 28
        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
        leftPadding: 5
        rightPadding: 5
        Layout.minimumWidth: implicitWidth

        contentItem: Item {
            implicitWidth: contentRow.implicitWidth
            implicitHeight: iconButton.stopMode ? stopStack.implicitHeight
                                                : contentRow.implicitHeight

            Row {
                id: contentRow
                // Not `visible`: an invisible row drops out of the layout, and
                // the width must stay the caption's in stop mode.
                opacity: iconButton.stopMode ? 0 : 1
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Icon {
                    visible: iconButton.iconName.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    name: iconButton.iconName
                    size: iconButton.iconSize
                    color: iconButton.highlight ? Theme.accentAttention
                          : (iconButton.enabled ? Theme.textPrimary : Theme.textMuted)
                }
                Text {
                    visible: iconButton.text.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: iconButton.text
                    font: iconButton.font
                    color: iconButton.highlight ? Theme.accentAttention
                          : (iconButton.enabled ? Theme.textPrimary : Theme.textMuted)
                }
            }

            Item {
                id: stopStack
                visible: iconButton.stopMode
                anchors.centerIn: parent
                implicitWidth: stopRing.width
                implicitHeight: stopRing.height

                Icon {
                    id: stopRing
                    name: "loader-circle"
                    size: 26
                    color: Theme.textSecondary

                    RotationAnimation on rotation {
                        from: 0
                        to: 360
                        duration: 900
                        loops: Animation.Infinite
                        running: iconButton.stopMode
                    }
                }

                Icon {
                    anchors.centerIn: stopRing
                    name: "square"
                    // Even sizes keep the centering on whole pixels — an odd
                    // square in an even ring lands at x.5 and renders left of
                    // the ring's center.
                    size: 12
                    color: Theme.textPrimary
                }
            }
        }
    }

    // A section label over a row of buttons. The group without a label (the
    // settings button) has no caption row to sit under, so it aligns to the
    // bottom of the toolbar — the row the labelled groups' buttons stand on.
    // fillWidth must be explicit: a nested layout is widened by default, and
    // the groups would spread over the whole toolbar.
    component ToolGroup: ColumnLayout {
        id: groupRoot

        property string title: ""
        property bool captionLine: true

        spacing: 3
        Layout.fillWidth: false
        Layout.alignment: groupRoot.captionLine ? Qt.AlignTop : Qt.AlignBottom

        LLOLabel {
            visible: groupRoot.captionLine
            // The caption's text edge follows the buttons' leftPadding, so it
            // never sticks out to the left of the content under it.
            leftPadding: 5
            text: groupRoot.title
            font: Theme.captionUpper
            color: Theme.textMuted
        }
    }

    // The group divider: a hairline over the full toolbar height — the
    // ToolSeparator's short center line reads as a gap, not a border.
    component ToolDivider: Rectangle {
        Layout.fillHeight: true
        implicitWidth: 1
        color: Theme.divider
    }

    Menu {
        id: fileMenu

        MenuItem {
            id: openProjectItem
            text: qsTr("Open project…")
            enabled: !Controller.busy && !Controller.importing && !Controller.exporting
                         && !Controller.projectBusy && !Controller.checkRunning
            onTriggered: headerRoot.openProjectRequested()
        }
        MenuSeparator {}
        MenuItem {
            id: saveItem

            action: Action {
                id: saveAction
                text: qsTr("Save project")
                shortcut: "Ctrl+S"
                enabled: Controller.hasImage && !Controller.busy && !Controller.importing
                         && !Controller.exporting && !Controller.projectBusy
                         && !Controller.checkRunning
                onTriggered: headerRoot.saveProjectRequested()
            }

            rightPadding: padding + spacing + saveShortcutLabel.implicitWidth

            Label {
                id: saveShortcutLabel
                anchors.right: parent.right
                anchors.rightMargin: saveItem.padding
                anchors.verticalCenter: parent.verticalCenter
                font: Theme.caption
                color: Theme.textMuted
                text: headerRoot.shortcutDisplayText(saveAction.shortcut)
            }
        }
        MenuItem {
            id: saveAsItem

            action: Action {
                id: saveAsAction
                text: qsTr("Save project as…")
                shortcut: "Ctrl+Shift+S"
                enabled: Controller.hasImage && !Controller.busy && !Controller.importing
                         && !Controller.exporting && !Controller.projectBusy
                         && !Controller.checkRunning
                onTriggered: headerRoot.saveProjectAsRequested()
            }

            rightPadding: padding + spacing + saveAsShortcutLabel.implicitWidth

            Label {
                id: saveAsShortcutLabel
                anchors.right: parent.right
                anchors.rightMargin: saveAsItem.padding
                anchors.verticalCenter: parent.verticalCenter
                font: Theme.caption
                color: Theme.textMuted
                text: headerRoot.shortcutDisplayText(saveAsAction.shortcut)
            }
        }
    }

    Menu {
        id: settingsMenu

        MenuItem {
            text: qsTr("Setup wizard")
            onTriggered: headerRoot.setupWizardRequested()
        }
        MenuItem {
            text: qsTr("Interface")
            onTriggered: headerRoot.openUiSettingsRequested()
        }
        MenuItem {
            text: qsTr("Output")
            onTriggered: headerRoot.openOutputSettingsRequested()
        }
        MenuItem {
            text: qsTr("Verification")
            onTriggered: headerRoot.openVerificationSettingsRequested()
        }
        MenuItem {
            text: qsTr("Runtime")
            onTriggered: headerRoot.openRuntimeSettingsRequested()
        }
        MenuItem {
            text: qsTr("Models")
            onTriggered: headerRoot.openTaskModelsSettingsRequested()
        }
    }

    // The markup pass replaces the pages' blocks (and drops their text), so a
    // document that already carries results asks first.
    InterruptConfirmDialog {
        id: markupConfirmDialog

        function requestMarkup() {
            // Only the pages in scope lose their blocks, so their results —
            // not another page's — are what the confirmation asks about.
            if (!Controller.scopeHasResults) {
                Controller.layoutScopePages()
                return
            }
            const n = Controller.selectedPageCount
            const all = n === 0 || n === Controller.pageCount
            title = qsTr("Mark up?")
            prompt = all ? qsTr("Marking up replaces the recognized blocks and text on every page. Continue?")
                         : qsTr("Marking up replaces the recognized blocks and text on the selected pages. Continue?")
            open()
        }

        onConfirmed: Controller.layoutScopePages()
    }

    // The auto pipeline starts with the markup pass, and the markup pass
    // replaces the pages' blocks (and drops their text) — the same loss the
    // markup button asks about.
    InterruptConfirmDialog {
        id: autoConfirmDialog

        function requestAutoPipeline() {
            if (!Controller.scopeHasResults) {
                Controller.startAutoPipeline()
                return
            }
            // An empty selection means the whole document, and the markup pass
            // wipes the blocks and the text of every page in scope.
            const n = Controller.selectedPageCount
            const all = n === 0 || n === Controller.pageCount
            title = qsTr("Automatic pipeline?")
            prompt = all ? qsTr("The pipeline replaces the blocks and the text on every page. Continue?")
                         : qsTr("The pipeline replaces the blocks and the text on the selected pages. Continue?")
            open()
        }

        onConfirmed: Controller.startAutoPipeline()
    }
} // ToolBar
