import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "Setup"

// First-run wizard (§4.4 / Stage F). A modal Dialog that walks a fresh profile
// through Managed setup: Welcome → Runtime → Model → Launch → Done.
//   - Closing via the X, Skip or window close sets setupDismissed = true.
//   - The External choice on Welcome finishes the wizard immediately
//     (setupVersion = 1) and asks the host to open Connection settings.
//   - Completing all steps sets setupVersion = 1.
Dialog {
    id: wizard
    modal: true
    anchors.centerIn: parent
    width: 660
    height: 560
    title: qsTr("Setup")

    // Step index 0..4.
    property int current: 0

    // True when the wizard was completed (Finish) or External was chosen, so a
    // subsequent close does not mark it dismissed.
    property bool completed: false

    // True when the active step allows advancing.
    readonly property bool canProceed: currentStep && currentStep.complete

    // Emitted when the user picks the External path on Welcome.
    signal openConnectionSettings()

    // Entry point (called by Main.qml and Settings → Runtime → «Запустить мастер»).
    // We must not shadow Dialog::open(), so this is a distinct name.
    function startWizard() {
        wizard.current = 0
        wizard.completed = false
        Settings.setupDismissed = false
        wizard.open()
    }

    // Dismiss via X / Skip: mark dismissed (only when not completed).
    function dismissWizard() {
        if (wizard.completed)
            return
        Settings.setupDismissed = true
        wizard.close()
    }

    // --- step host ----------------------------------------------------------
    StackLayout {
        id: stackLayout
        anchors.fill: parent
        currentIndex: wizard.current

        StepWelcome {
            id: stepWelcome
            onExternalChosen: {
                // External = set up; do not mark dismissed on the coming close.
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
        Button {
            text: qsTr("Skip")
            visible: wizard.current === 0
            onClicked: wizard.dismissWizard()
        }
        Button {
            text: qsTr("Back")
            visible: wizard.current > 0 && wizard.current < 4
            onClicked: wizard.current = Math.max(0, wizard.current - 1)
        }
        Button {
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

    // X / Esc closes the wizard (Popup.closed fires for both accept and reject;
    // the completed flag prevents marking it dismissed when finishing normally).
    onRejected: wizard.dismissWizard()
    onClosed: wizard.dismissWizard()
}