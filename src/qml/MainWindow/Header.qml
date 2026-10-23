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
    // document; the «Страницы» field is the same selection written as a range.
    readonly property int scopePageCount: Controller.selectedPageCount > 0
                                          ? Controller.selectedPageCount
                                          : Controller.pageCount

    // The «Авто» quality tier is kept for the automatic pipelines; until they
    // land the switch is live but its button stays disabled.
    property int autoMode: 0

    // The page-recognition button that was used last: while a page-recognition
    // run is active the stop affordance shows on it (the running queue does
    // not expose which role it serves).
    property string pageRecognitionTask: "pageOcr"

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
            captionLine: false

            IconToolButton {
                id: fileButton
                text: qsTr("File")
                iconName: "file-text"
                Layout.fillWidth: true
                onClicked: fileMenu.popup(fileButton, 0, fileButton.height + 2)
            }

            IconToolButton {
                id: importButton
                text: qsTr("Import")
                iconName: "image"
                Layout.fillWidth: true
                enabled: !Controller.busy && !Controller.importing && !Controller.exporting
                         && !Controller.checkBusy
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Open images, PDF or DjVu")
                onClicked: headerRoot.openFileRequested()
            }
        }

        ToolDivider {}

        ToolGroup {
            title: qsTr("Pages")

            TextField {
                id: pagesField

                implicitWidth: 110
                implicitHeight: 28
                enabled: Controller.hasImage
                placeholderText: qsTr("All")
                ToolTip.visible: hovered && enabled
                ToolTip.text: qsTr("Pages to process: follows the selection in the thumbnail panel, or type a range like 1-10, 15")

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
        }

        ToolDivider {}

        ToolGroup {
            title: qsTr("Auto")

            SegmentedControl {
                labels: [qsTr("Fast"), qsTr("Balanced"), qsTr("Accurate")]
                currentIndex: headerRoot.autoMode
                onActivated: (index) => headerRoot.autoMode = index
            }

            IconToolButton {
                text: qsTr("Recognize %1 pg.").arg(headerRoot.scopePageCount)
                iconName: "zap"
                Layout.fillWidth: true
                enabled: false
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Automatic pipelines arrive in a later step")
            }
        }

        ToolDivider {}

        ToolGroup {
            title: qsTr("Markup")

            IconToolButton {
                text: qsTr("Mark up")
                iconName: "layout-template"
                Layout.fillWidth: true
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
                Layout.fillWidth: true
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

        ToolDivider {}

        ToolGroup {
            title: qsTr("Recognize")

            IconToolButton {
                text: qsTr("With markup")
                iconName: "zap"
                Layout.fillWidth: true
                stopMode: Controller.busy && headerRoot.pageRecognitionTask === "pageOcr"
                enabled: stopMode || (headerRoot.pipelineIdle && Controller.hasImage
                         && Controller.canRecognize && Controller.pageOcrModelAvailable)
                ToolTip.visible: hovered
                ToolTip.text: stopMode ? qsTr("Stop the recognition")
                                       : qsTr("Recognize the selected pages: blocks and their text in one pass")
                onClicked: {
                    if (stopMode) {
                        Controller.stop()
                        return
                    }
                    headerRoot.pageRecognitionTask = "pageOcr"
                    Controller.recognizeScopePages("pageOcr")
                }
            }

            IconToolButton {
                text: qsTr("Without markup")
                iconName: "type"
                Layout.fillWidth: true
                stopMode: Controller.busy && headerRoot.pageRecognitionTask === "rawPageOcr"
                enabled: stopMode || (headerRoot.pipelineIdle && Controller.hasImage
                         && Controller.canRecognize && Controller.rawPageOcrModelAvailable)
                ToolTip.visible: hovered
                ToolTip.text: stopMode ? qsTr("Stop the recognition")
                                       : qsTr("Recognize the selected pages as raw text")
                onClicked: {
                    if (stopMode) {
                        Controller.stop()
                        return
                    }
                    headerRoot.pageRecognitionTask = "rawPageOcr"
                    Controller.recognizeScopePages("rawPageOcr")
                }
            }
        }

        ToolDivider {}

        ToolGroup {
            title: qsTr("Verification")

            IconToolButton {
                text: qsTr("Verify")
                iconName: "circle-check"
                Layout.fillWidth: true
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
                Layout.fillWidth: true
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
        }

        ToolDivider {}

        ToolGroup {
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

        Item { Layout.fillWidth: true }

        ToolGroup {
            captionLine: false

            IconToolButton {
                id: settingsButton
                text: qsTr("Settings")
                iconName: "settings"
                onClicked: settingsMenu.popup(settingsButton, 0, settingsButton.height + 2)
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
    component IconToolButton: ToolButton {
        id: iconButton

        property string iconName: ""
        property int iconSize: 15
        property bool highlight: false
        property bool stopMode: false

        implicitHeight: 28
        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
        leftPadding: 6
        rightPadding: 6
        Layout.minimumWidth: implicitWidth

        contentItem: Item {
            implicitWidth: iconButton.stopMode ? stopStack.implicitWidth
                                               : contentRow.implicitWidth
            implicitHeight: iconButton.stopMode ? stopStack.implicitHeight
                                                : contentRow.implicitHeight

            Row {
                id: contentRow
                visible: !iconButton.stopMode
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5

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

    // A caption over a stack of pipeline buttons; the children declared at the
    // use site stack vertically below it. The caption line exists even when
    // the title is empty (Экспорт rides on the pipeline buttons' first row);
    // groups that opt out start at the very top of the toolbar (Файл/Импорт,
    // Настройки). fillWidth must be explicit: a nested layout is widened by
    // default, and the groups would spread over the whole toolbar.
    component ToolGroup: ColumnLayout {
        id: groupRoot

        property string title: ""
        property bool captionLine: true

        spacing: 3
        Layout.fillWidth: false
        Layout.alignment: Qt.AlignTop

        LLOLabel {
            visible: groupRoot.captionLine
            // The caption's text edge follows the buttons' leftPadding, so it
            // never sticks out to the left of the content under it.
            leftPadding: 6
            text: groupRoot.title
            font: Theme.caption
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
        MenuSeparator {}
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
} // ToolBar
