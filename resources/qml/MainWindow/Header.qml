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

    // The scope caption after the action names: an empty selection means the
    // whole document.
    function scopeCaption() {
        if (!Controller.hasImage)
            return ""
        const n = Controller.selectedPageCount
        if (n === 0 || n === Controller.pageCount)
            return qsTr("all %1").arg(Controller.pageCount)
        return qsTr("%1 pg.").arg(n)
    }

    function scopedText(base) {
        const caption = scopeCaption()
        return caption.length > 0 ? base + " " + caption : base
    }

    // The pipeline preset selector is a stub until the automatic pipelines
    // land: the manual mode is what the step buttons below already are.
    property bool manualPreset: true

    // What the main click runs, chosen in «Распознать ▾»: 0 = the full page
    // pass that produces the blocks and their text, 1 = the text of the
    // already-marked blocks, 2 = raw text without blocks. The chosen menu
    // item is mirrored onto the button caption; a mode is selectable only
    // while its inputs exist, so the button can never sit on a dead choice.
    property int recognizeMode: 0

    function recognizeModeName() {
        if (recognizeMode === 1)
            return qsTr("By the marked blocks")
        if (recognizeMode === 2)
            return qsTr("Without markup")
        return qsTr("With markup")
    }

    function recognizeModeTip() {
        if (recognizeMode === 1)
            return qsTr("Recognize the marked blocks that still have no text")
        if (recognizeMode === 2)
            return qsTr("Recognize the selected pages as raw text")
        return qsTr("Recognize the selected pages: blocks and their text in one pass")
    }

    // The pipeline buttons' idle gate: recognition machinery plus the model
    // download queue (which shares the footer line and the Stop key).
    readonly property bool pipelineIdle: Controller.actionsIdle
                                         && !ModelInstaller.taskQueueActive

    readonly property bool recognizeMainEnabled: headerRoot.pipelineIdle && Controller.hasImage
        && (recognizeMode === 1 ? Controller.scopeBlocksRecognitionSupported && Controller.blockRecognitionModelAvailable
        : recognizeMode === 2 ? Controller.canRecognize && Controller.rawPageOcrModelAvailable
        : Controller.canRecognize && Controller.pageOcrModelAvailable)

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
        anchors.fill: parent
        spacing: Theme.spacingSmall

        ToolButton {
            id: fileButton
            text: qsTr("File")
            Layout.minimumWidth: implicitWidth
            onClicked: fileMenu.popup(fileButton, 0, fileButton.height + 2)
        }

        ToolSeparator {}

        RowLayout {
            id: presetGroup
            // The stub stays in place while a run is active; it just goes
            // inactive like the pipeline buttons.
            enabled: !TaskStatus.running()
            spacing: Theme.spacingSmall

            LLOLabel {
                text: qsTr("Preset:")
                color: Theme.textSecondary
            }

            IconToolButton {
                id: presetButton
                iconName: "wrench"
                text: qsTr("Manual") + " \u25be"
                enabled: true
                onClicked: presetMenu.popup(presetButton, 0, presetButton.height + 2)
            }

            IconToolButton {
                text: qsTr("Auto-recognize")
                iconName: "play"
                enabled: false
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Automatic pipelines arrive in a later step")
            }
        }

        ToolSeparator {}

        IconToolButton {
            text: headerRoot.scopedText(qsTr("Mark up"))
            iconName: "layout-template"
            Layout.minimumWidth: implicitWidth
            enabled: headerRoot.pipelineIdle && Controller.hasImage
                     && Controller.layoutAvailable && Controller.layoutModelAvailable
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Find the blocks on the selected pages")
            onClicked: markupConfirmDialog.requestMarkup()
        }

        SplitToolButton {
            text: headerRoot.scopedText(headerRoot.recognizeModeName())
            iconName: "zap"
            mainTip: headerRoot.recognizeModeTip()
            mainEnabled: headerRoot.recognizeMainEnabled
            arrowEnabled: headerRoot.pipelineIdle && Controller.hasImage
            onMainActivated: {
                if (recognizeMode === 1)
                    Controller.recognizeScopeBlocks()
                else if (recognizeMode === 2)
                    Controller.recognizeScopePages("rawPageOcr")
                else
                    Controller.recognizeScopePages("pageOcr")
            }
            onMenuRequested: (anchor) => recognizeMenu.popup(anchor, 0, anchor.height + 2)
        }

        SplitToolButton {
            text: headerRoot.scopedText(qsTr("Verify"))
            iconName: "circle-check"
            mainTip: qsTr("Verify the recognized blocks on the selected pages")
            mainEnabled: headerRoot.pipelineIdle && Controller.hasImage
                         && Controller.scopeVerificationSupported && Controller.decisionModelAvailable
            arrowEnabled: mainEnabled
            onMainActivated: Controller.checkScopeEnabledBlocks()
            onMenuRequested: (anchor) => verifyMenu.popup(anchor, 0, anchor.height + 2)
        }

        IconToolButton {
            text: Controller.scopedProblemBlockCount > 0
                  ? qsTr("Fix") + " (" + Controller.scopedProblemBlockCount + ")"
                  : qsTr("Fix")
            iconName: "wand-sparkles"
            highlight: Controller.scopedProblemBlockCount > 0
            Layout.minimumWidth: implicitWidth
            enabled: headerRoot.pipelineIdle && Controller.hasImage
                     && Controller.fixModelAvailable && Controller.scopedProblemBlockCount > 0
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Fix the problem blocks on the selected pages")
            onClicked: Controller.fixScopeProblemBlocks()
        }

        // The toolbar itself carries no progress text (the footer shows it);
        // while anything runs the Stop key appears here.
        IconToolButton {
            text: qsTr("Stop")
            iconName: "square"
            iconSize: 13
            visible: Controller.busy || Controller.checkBusy || Controller.layoutBusy
                     || ModelInstaller.taskQueueActive
            onClicked: {
                if (Controller.busy)
                    Controller.stop()
                else if (Controller.layoutBusy)
                    Controller.stopLayout()
                else if (Controller.checkBusy)
                    Controller.stopCheck()
                else
                    ModelInstaller.cancelTaskInstalls()
            }
        }

        Item { Layout.fillWidth: true }

        SplitToolButton {
            text: qsTr("Export")
            iconName: "upload"
            mainTip: qsTr("Export the whole document")
            mainEnabled: Controller.hasResult && !Controller.exporting && !Controller.importing
                         && !Controller.checkBusy
            arrowEnabled: mainEnabled
            onMainActivated: headerRoot.exportRequested(0)
            onMenuRequested: (anchor) => exportMenu.popup(anchor, 0, anchor.height + 2)
        }

        IconToolButton {
            id: settingsButton
            iconName: "settings"
            Accessible.name: qsTr("Settings")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Settings")
            onClicked: settingsMenu.popup(settingsButton, 0, settingsButton.height + 2)
        }
    }//RowLayout

    // A toolbar button with a Lucide glyph before its caption; the highlight
    // turns the caption into the warm accent (the «Исправить» badge). The
    // implicit width must come from the content: a custom contentItem alone
    // sizes the control to its padding, and the toolbar piles the buttons up.
    component IconToolButton: ToolButton {
        id: iconButton

        property string iconName: ""
        property int iconSize: 15
        property bool highlight: false

        implicitHeight: 28
        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
        leftPadding: 6
        rightPadding: 6
        Layout.minimumWidth: implicitWidth

        contentItem: Item {
            implicitWidth: contentRow.implicitWidth
            implicitHeight: contentRow.implicitHeight

            Row {
                id: contentRow
                anchors.centerIn: parent
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
        }
    }

    Menu {
        id: fileMenu

        MenuItem {
            id: openItem

            action: Action {
                id: openAction
                text: qsTr("Open…")
                shortcut: "Ctrl+O"
                enabled: !Controller.busy && !Controller.importing && !Controller.exporting
                         && !Controller.checkBusy
                onTriggered: headerRoot.openFileRequested()
            }

            rightPadding: padding + spacing + openShortcutLabel.implicitWidth

            Label {
                id: openShortcutLabel
                anchors.right: parent.right
                anchors.rightMargin: openItem.padding
                anchors.verticalCenter: parent.verticalCenter
                font: Theme.caption
                color: Theme.textMuted
                text: headerRoot.shortcutDisplayText(openAction.shortcut)
            }
        }
        MenuItem {
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
        MenuSeparator {}
        MenuItem {
            text: qsTr("Export…")
            enabled: Controller.hasResult && !Controller.exporting && !Controller.importing
                     && !Controller.checkBusy
            onTriggered: headerRoot.exportRequested(0)
        }
    }

    Menu {
        id: presetMenu

        MenuItem {
            text: qsTr("Plain text")
            enabled: false
        }
        MenuItem {
            text: qsTr("Complex layout")
            enabled: false
        }
        MenuItem {
            text: qsTr("Technical / scientific")
            enabled: false
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Manual (step by step)")
            checkable: true
            checked: headerRoot.manualPreset
        }
    }

    Menu {
        id: recognizeMenu

        MenuItem {
            text: qsTr("With markup")
            checkable: true
            checked: headerRoot.recognizeMode === 0
            enabled: headerRoot.pipelineIdle && Controller.hasImage
                     && Controller.canRecognize && Controller.pageOcrModelAvailable
            onTriggered: headerRoot.recognizeMode = 0
        }
        MenuItem {
            text: qsTr("By the marked blocks")
            checkable: true
            checked: headerRoot.recognizeMode === 1
            enabled: headerRoot.pipelineIdle && Controller.hasImage
                     && Controller.scopeBlocksRecognitionSupported
                     && Controller.blockRecognitionModelAvailable
            onTriggered: headerRoot.recognizeMode = 1
        }
        MenuItem {
            text: qsTr("Without markup")
            checkable: true
            checked: headerRoot.recognizeMode === 2
            enabled: headerRoot.pipelineIdle && Controller.hasImage
                     && Controller.canRecognize && Controller.rawPageOcrModelAvailable
            onTriggered: headerRoot.recognizeMode = 2
        }
    }

    Menu {
        id: verifyMenu

        MenuItem {
            text: qsTr("Verify only")
            enabled: headerRoot.pipelineIdle && Controller.hasImage
                     && Controller.scopeVerificationSupported && Controller.decisionModelAvailable
            onTriggered: Controller.checkScopeEnabledBlocks(VerificationQueueController.FixSkip)
        }
    }

    Menu {
        id: exportMenu

        MenuItem {
            text: qsTr("Whole document…")
            enabled: Controller.hasResult && !Controller.exporting && !Controller.importing
                     && !Controller.checkBusy
            onTriggered: headerRoot.exportRequested(0)
        }
        MenuItem {
            text: qsTr("Current page…")
            enabled: Controller.hasResult && !Controller.exporting && !Controller.importing
                     && !Controller.checkBusy
            onTriggered: headerRoot.exportRequested(1)
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
