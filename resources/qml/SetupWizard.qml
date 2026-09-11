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
    readonly property bool canProceed: currentStep && currentStep.complete

    signal openConnectionSettings()

    function startWizard() {
        wizard.current = 0
        wizard.completed = false
        Settings.setupDismissed = false
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

    property Item currentStep
    onCurrentChanged: wizard.currentStep = stackLayout.children[wizard.current]
    Component.onCompleted: wizard.currentStep = stackLayout.children[0]

    footer: DialogButtonBox {
        LLOButton {
            text: qsTr("Skip")
            visible: wizard.current === 0
            onClicked: wizard.dismissWizard()
        }
        LLOButton {
            text: qsTr("Back")
            visible: wizard.current > 0 && wizard.current < 4
            onClicked: wizard.current = Math.max(0, wizard.current - 1)
        }
        LLOButton {
            text: wizard.current === 4 ? qsTr("Finish") : qsTr("Next")
            enabled: wizard.canProceed
            onClicked: {
                if (wizard.current === 4) {
                    wizard.completed = true
                    stepDone.markDone()
                    wizard.accept()
                } else {
                    wizard.current += 1
                }
            }
        }
    }

    onRejected: wizard.dismissWizard()
    onClosed: wizard.dismissWizard()
}