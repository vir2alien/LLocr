pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "Setup"
import "Common"

Dialog {
    id: wizard
    modal: true
    // Created through a zero-size Loader (QML-13): without the explicit
    // overlay parent "parent" is the Loader, and centerIn pins the dialog
    // to the window's top-left corner.
    parent: Overlay.overlay
    anchors.centerIn: parent
    // Wide enough for the hardware step's task table (ADR 170).
    width: 760
    // Each step reports its content height (implicitHeight + margins); the
    // dialog grows past 560 when a long localized step needs it — wrapped
    // Russian hints otherwise clip — and never exceeds the screen.
    height: Math.max(560, Math.min(Overlay.overlay ? Overlay.overlay.height - 24 : 560,
                                   implicitHeight))
    title: qsTr("Setup")

    property int current: 0
    property bool completed: false
    // Step list depends on the mode chosen on step 2 (the external mode has
    // no hardware/check steps).
    property list<Item> steps: []
    // What the hardware step could not activate on its Next — quantizations
    // still to download; the check step's chain downloads them.
    property var missingChoices: []
    readonly property int lastStep: steps.length - 1
    // `var`, not Item: the steps are heterogeneous components that each declare
    // their own `complete`, which the static Item type cannot express. The only
    // other use is an identity comparison, which a var keeps working.
    readonly property var currentStep: steps.length ? steps[current] : null
    readonly property var canProceed: !!(currentStep && currentStep.complete)

    function rebuildSteps() {
        const choice = stepWelcome.choice
        if (choice === 1)
            // External server: configure the endpoint, then output options.
            steps = [stepIntro, stepWelcome, stepExternal, stepOutput]
        else
            steps = [stepIntro, stepWelcome, stepHardware, stepOutput, stepCheck]
    }

    function startWizard() {
        wizard.completed = false
        wizard.missingChoices = []
        stepWelcome.choice = -1
        rebuildSteps()
        wizard.current = 0
        Settings.setupDismissed = false
        RuntimeInstaller.rescanInstalledBuilds()
        wizard.open()
    }

    function dismissWizard() {
        if (wizard.completed)
            return
        Settings.setupDismissed = true
        wizard.close()
    }

    function finishWizard() {
        wizard.completed = true
        Settings.setupVersion = 1
        Settings.setupDismissed = false
        wizard.accept()
    }

    Component.onCompleted: rebuildSteps()

    contentItem: Item {
        id: stepArea
        implicitHeight: wizard.currentStep ? wizard.currentStep.implicitHeight : 0

        // Each step fills the area; exactly one (steps[current]) is visible.
        StepIntro {
            id: stepIntro
            anchors.fill: parent
            visible: wizard.currentStep === stepIntro
        }
        StepWelcome {
            id: stepWelcome
            anchors.fill: parent
            visible: wizard.currentStep === stepWelcome
        }
        StepHardware {
            id: stepHardware
            anchors.fill: parent
            visible: wizard.currentStep === stepHardware
        }

        StepExternal {
            id: stepExternal
            anchors.fill: parent
            visible: wizard.currentStep === stepExternal
        }
        StepOutput {
            id: stepOutput
            anchors.fill: parent
            visible: wizard.currentStep === stepOutput
        }
        StepCheck {
            id: stepCheck
            anchors.fill: parent
            visible: wizard.currentStep === stepCheck
            missingChoices: wizard.missingChoices
        }
    }

    footer: Rectangle {
        implicitHeight: footerRow.implicitHeight + 2 * Theme.spacingLarge
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.divider
        }

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.leftMargin: Theme.paddingWindow
            anchors.rightMargin: Theme.paddingWindow
            anchors.topMargin: Theme.spacingLarge
            anchors.bottomMargin: Theme.spacingLarge
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Step %1 of %2").arg(wizard.current + 1)
                                             .arg(wizard.steps.length)
                font.pointSize: Theme.captionSize
                color: Theme.textSecondary
            }
            LLOButton {
                text: qsTr("Skip")
                subtle: true
                visible: wizard.current === 0
                onClicked: wizard.dismissWizard()
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                text: qsTr("Back")
                visible: wizard.current > 0 && wizard.current < wizard.lastStep
                onClicked: {
                    // Going back reconsiders the mode choice: the endpoint
                    // typed on the external step is abandoned, not saved.
                    if (wizard.currentStep === stepExternal)
                        stepExternal.revertValues()
                    wizard.current = Math.max(0, wizard.current - 1)
                }
            }
            LLOButton {
                text: wizard.current === wizard.lastStep ? qsTr("Finish") : qsTr("Next")
                emphasis: true
                enabled: wizard.canProceed
                onClicked: {
                    if (wizard.current === wizard.lastStep) {
                        wizard.finishWizard()
                    } else {
                        // The step list may change after the mode is picked
                        // (step 2) — rebuild before moving on.
                        wizard.rebuildSteps()
                        // Leaving the hardware step applies the task choices;
                        // the check step downloads what is still missing.
                        if (wizard.currentStep === stepHardware)
                            wizard.missingChoices = stepHardware.applyChoices()
                        wizard.current += 1
                    }
                }
            }
        }
    }

    onClosed: wizard.dismissWizard()
}
