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
    signal openSettingsRequested()

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
                spacing: Theme.spacingSmall

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
                spacing: Theme.spacingSmall

                TextField {
                    id: pagesField
                    font: Theme.bodySmall
                    implicitWidth: 120
                    implicitHeight: 22
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
                            if (!Controller.scopeHasResults) {
                                Controller.startAutoPipeline(Controller.RerunMode.SkipDone)
                                return
                            }
                            autoRerunDialog.requestAutoPipeline()
                        }
                    }
                }
            }
        }

        ToolDivider {}

        ToolGroup {
            title: qsTr("Markup and OCR")

            RowLayout {
                spacing: Theme.spacingSmall

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
                        if (!Controller.scopeHasResults) {
                            Controller.layoutScopePages(Controller.RerunMode.SkipDone)
                            return
                        }
                        markupRerunDialog.requestMarkup()
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
                        if (!Controller.scopeRecognitionHasResults) {
                            Controller.recognizeScopeBlocks(Controller.RerunMode.SkipDone)
                            return
                        }
                        recognitionRerunDialog.requestRecognition()
                    }
                }
            }
        }

        ToolDivider {}

        ToolGroup {
            title: qsTr("Verification and AI correction")

            RowLayout {
                spacing: Theme.spacingSmall

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
                        if (!Controller.scopeCheckHasResults) {
                            Controller.checkScopeEnabledBlocks(Controller.RerunMode.SkipDone)
                            return
                        }
                        checkRerunDialog.requestCheck()
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
                        if (!Controller.scopeFixHasResults) {
                            Controller.fixScopeProblemBlocks(Controller.RerunMode.SkipDone)
                            return
                        }
                        fixRerunDialog.requestFix()
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
                        if (!Controller.scopeReadHasResults) {
                            Controller.readScopeBlocks(Controller.RerunMode.SkipDone)
                            return
                        }
                        readRerunDialog.requestRead()
                    }
                }
            }
        }

        ToolDivider {}

        Item { Layout.fillWidth: true }

        ToolGroup {
            captionLine: false

            RowLayout {
                spacing: Theme.spacingSmall

                IconToolButton {
                    id: settingsButton
                    text: qsTr("Settings")
                    iconName: "settings"
                    onClicked: headerRoot.openSettingsRequested()
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

        font: Theme.button
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
                spacing: Theme.spacingSmall

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
                    size: Theme.iconSizeXlarge
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
                    size: Theme.iconSizeSmall
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

        spacing: Theme.spacingTiny
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
            font: Theme.button
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
                font: Theme.button
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
                font: Theme.button
                color: Theme.textMuted
                text: headerRoot.shortcutDisplayText(saveAsAction.shortcut)
            }
        }
    }

    // The markup pass replaces the pages' blocks (and drops their text), so
    // a scope that already carries results asks whether to redo it or to
    // mark up only the pages that still have none.
    RerunConfirmDialog {
        id: markupRerunDialog

        function requestMarkup() {
            // Only the pages in scope lose their blocks, so their results —
            // not another page's — are what the confirmation asks about.
            const n = Controller.selectedPageCount
            const all = n === 0 || n === Controller.pageCount
            title = qsTr("Mark up?")
            prompt = all ? qsTr("Every page already carries markup and results. Redo the markup (it replaces the blocks and all results) or mark up only the pages that still have none?")
                         : qsTr("The selected pages already carry markup and results. Redo the markup (it replaces the blocks and all results) or mark up only the pages that still have none?")
            open()
        }

        onRedoRequested: Controller.layoutScopePages(Controller.RerunMode.RedoAll)
        onContinueRequested: Controller.layoutScopePages(Controller.RerunMode.SkipDone)
    }

    // The auto pipeline starts with the markup pass, and the markup pass
    // replaces the pages' blocks (and drops their text) — the same loss the
    // markup button asks about.
    RerunConfirmDialog {
        id: autoRerunDialog

        function requestAutoPipeline() {
            // An empty selection means the whole document, and the redo wipes
            // the blocks and the text of every page in scope.
            const n = Controller.selectedPageCount
            const all = n === 0 || n === Controller.pageCount
            title = qsTr("Automatic pipeline?")
            prompt = all ? qsTr("Every page already carries results. Redo everything from the start (the markup runs again, all results are erased) or fill in only what is missing?")
                         : qsTr("The selected pages already carry results. Redo everything from the start (the markup runs again, all results are erased) or fill in only what is missing?")
            open()
        }

        onRedoRequested: Controller.startAutoPipeline(Controller.RerunMode.RedoAll)
        onContinueRequested: Controller.startAutoPipeline(Controller.RerunMode.SkipDone)
    }

    // The redo erases the text of the blocks it runs over — and with it their
    // corrections and verdicts.
    RerunConfirmDialog {
        id: recognitionRerunDialog

        function requestRecognition() {
            const n = Controller.selectedPageCount
            const all = n === 0 || n === Controller.pageCount
            title = qsTr("Recognize blocks?")
            prompt = all ? qsTr("The blocks on every page are already recognized. Redo the recognition (it erases the text, the corrections and the verdicts of these blocks) or recognize only the blocks that still have no text?")
                         : qsTr("The blocks on the selected pages are already recognized. Redo the recognition (it erases the text, the corrections and the verdicts of these blocks) or recognize only the blocks that still have no text?")
            open()
        }

        onRedoRequested: Controller.recognizeScopeBlocks(Controller.RerunMode.RedoAll)
        onContinueRequested: Controller.recognizeScopeBlocks(Controller.RerunMode.SkipDone)
    }

    // The redo erases the verdicts the verification has already given.
    RerunConfirmDialog {
        id: checkRerunDialog

        function requestCheck() {
            const n = Controller.selectedPageCount
            const all = n === 0 || n === Controller.pageCount
            title = qsTr("Verify?")
            prompt = all ? qsTr("Some of the blocks on every page are already verified. Redo the verification (the current verdicts are erased and the blocks are verified again) or verify only the blocks without a verdict?")
                         : qsTr("Some of the blocks on the selected pages are already verified. Redo the verification (the current verdicts are erased and the blocks are verified again) or verify only the blocks without a verdict?")
            open()
        }

        onRedoRequested: Controller.checkScopeEnabledBlocks(Controller.RerunMode.RedoAll)
        onContinueRequested: Controller.checkScopeEnabledBlocks(Controller.RerunMode.SkipDone)
    }

    // The redo erases the stored corrections and repeats the fix over the
    // recognized text.
    RerunConfirmDialog {
        id: fixRerunDialog

        function requestFix() {
            const n = Controller.selectedPageCount
            const all = n === 0 || n === Controller.pageCount
            title = qsTr("Fix?")
            prompt = all ? qsTr("Some of the blocks on every page are already fixed. Redo the fix (it erases the corrections and repeats them over the recognized text) or fix only the blocks that still wait for a fix?")
                         : qsTr("Some of the blocks on the selected pages are already fixed. Redo the fix (it erases the corrections and repeats them over the recognized text) or fix only the blocks that still wait for a fix?")
            open()
        }

        onRedoRequested: Controller.fixScopeProblemBlocks(Controller.RerunMode.RedoAll)
        onContinueRequested: Controller.fixScopeProblemBlocks(Controller.RerunMode.SkipDone)
    }

    // The redo erases the reader's previous answers.
    RerunConfirmDialog {
        id: readRerunDialog

        function requestRead() {
            const n = Controller.selectedPageCount
            const all = n === 0 || n === Controller.pageCount
            title = qsTr("Re-recognize?")
            prompt = all ? qsTr("Some of the blocks on every page have already been re-recognized. Redo the re-reading (it erases its results) or re-recognize only the blocks that have not been re-recognized yet?")
                         : qsTr("Some of the blocks on the selected pages have already been re-recognized. Redo the re-reading (it erases its results) or re-recognize only the blocks that have not been re-recognized yet?")
            open()
        }

        onRedoRequested: Controller.readScopeBlocks(Controller.RerunMode.RedoAll)
        onContinueRequested: Controller.readScopeBlocks(Controller.RerunMode.SkipDone)
    }
} // ToolBar
