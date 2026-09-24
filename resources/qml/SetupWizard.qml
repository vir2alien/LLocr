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
    anchors.centerIn: parent
    width: 660
    height: 560
    title: qsTr("Setup")

    property int current: 0
    property bool completed: false
    // Step list depends on the mode chosen on step 2 (the model step is
    // skipped for the external-server mode).
    property list<Item> steps: []
    readonly property int lastStep: steps.length - 1
    readonly property Item currentStep: steps.length ? steps[current] : null
    readonly property bool canProceed: currentStep && currentStep.complete

    function rebuildSteps() {
        const choice = stepWelcome.choice
        if (choice === 2)
            // External server: no model and no managed-launch steps.
            steps = [stepIntro, stepWelcome, stepExternal, stepOutput, stepDone]
        else if (choice === 1)
            steps = [stepIntro, stepWelcome, stepBinary, stepModel, stepOutput,
                     stepLaunch, stepDone]
        else
            steps = [stepIntro, stepWelcome, stepRuntime, stepModel, stepOutput,
                     stepLaunch, stepDone]
    }

    function startWizard() {
        wizard.completed = false
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

    Component.onCompleted: rebuildSteps()

    Item {
        anchors.fill: parent

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
        StepRuntime {
            id: stepRuntime
            anchors.fill: parent
            visible: wizard.currentStep === stepRuntime
        }
        StepBinary {
            id: stepBinary
            anchors.fill: parent
            visible: wizard.currentStep === stepBinary
        }
        StepExternal {
            id: stepExternal
            anchors.fill: parent
            visible: wizard.currentStep === stepExternal
        }
        StepModel {
            id: stepModel
            anchors.fill: parent
            visible: wizard.currentStep === stepModel
        }
        StepOutput {
            id: stepOutput
            anchors.fill: parent
            visible: wizard.currentStep === stepOutput
        }
        StepLaunch {
            id: stepLaunch
            anchors.fill: parent
            visible: wizard.currentStep === stepLaunch
        }
        StepDone {
            id: stepDone
            anchors.fill: parent
            visible: wizard.currentStep === stepDone
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
                onClicked: wizard.current = Math.max(0, wizard.current - 1)
            }
            LLOButton {
                text: wizard.current === wizard.lastStep ? qsTr("Finish") : qsTr("Next")
                emphasis: true
                enabled: wizard.canProceed
                onClicked: {
                    if (wizard.current === wizard.lastStep) {
                        wizard.completed = true
                        stepDone.markDone()
                        wizard.accept()
                    } else {
                        // The step list may change after the mode is picked
                        // (step 2) — rebuild before moving on.
                        wizard.rebuildSteps()
                        wizard.current += 1
                    }
                }
            }
        }
    }

    onRejected: wizard.dismissWizard()
    onClosed: wizard.dismissWizard()
}
