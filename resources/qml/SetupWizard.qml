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
    property list<Item> steps: [stepWelcome, stepRuntime, stepModel, stepLaunch, stepDone]
    readonly property int lastStep: steps.length - 1
    readonly property Item currentStep: steps[current]
    readonly property bool canProceed: currentStep && currentStep.complete

    signal openConnectionSettings()

    function startWizard() {
        wizard.current = 0
        wizard.completed = false
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

    StackLayout {
        id: stackLayout
        anchors.fill: parent
        currentIndex: wizard.current

        StepWelcome {
            id: stepWelcome
            onExternalChosen: {
                wizard.completed = true
                wizard.close()
                wizard.openConnectionSettings()
            }
        }
        StepRuntime { id: stepRuntime }
        StepModel { id: stepModel }
        StepLaunch { id: stepLaunch }
        StepDone { id: stepDone }
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
                        wizard.current += 1
                    }
                }
            }
        }
    }

    onRejected: wizard.dismissWizard()
    onClosed: wizard.dismissWizard()
}