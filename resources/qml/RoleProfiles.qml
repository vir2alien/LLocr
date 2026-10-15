pragma Singleton

import QtQml

import LLocr

// Task-keyed dispatch for the six model tasks ("pageOcr", "rawPageOcr",
// "blockRecognition", "decision", "layout", "fix"): every task-conditional
// binding goes through here, so a new task is one edit in one file. The reads
// stay inside the functions, which is what keeps the callers' bindings
// reactive.
QtObject {
    function title(task) {
        return task === "rawPageOcr" ? qsTr("Recognition without markup")
             : task === "pageOcr" ? qsTr("Recognition with markup")
             : task === "blockRecognition" ? qsTr("Block recognition")
             : task === "decision" ? qsTr("Verification")
             : task === "layout" ? qsTr("Markup")
             : qsTr("Block fix")
    }

    function samplingHint(task) {
        return task === "decision" ? qsTr("Request parameters for the decision endpoint. The set comes from the model profile — edit the values, not the list.")
             : task === "blockRecognition" ? qsTr("Sampling parameters sent with every block OCR request. The set comes from the model profile — edit the values, not the list.")
             : task === "layout" ? qsTr("Sampling parameters sent with every page markup request. The set comes from the model profile — edit the values, not the list.")
             : task === "rawPageOcr" ? qsTr("Sampling parameters sent with every plain-text page recognition request. The set comes from the model profile — edit the values, not the list.")
             : task === "fix" ? qsTr("Sampling parameters sent with every block fix request. The set comes from the model profile — edit the values, not the list.")
             : qsTr("Sampling parameters sent with every recognition request. The set comes from the model profile — edit the values, not the list.")
    }

    function quantModels(task) {
        return task === "all" ? ModelInstaller.allModels
             : task === "blockRecognition" ? ModelInstaller.checkQuantModels
             : task === "decision" ? ModelInstaller.decisionQuantModels
             : task === "layout" ? ModelInstaller.layoutQuantModels
             : task === "rawPageOcr" ? ModelInstaller.rawPageOcrQuantModels
             : task === "fix" ? ModelInstaller.fixQuantModels
             : ModelInstaller.quantModels
    }

    function runtimeNote(task) {
        return LaunchProfiles.modelRuntimeNoteForRole(task)
    }

    function profiles(task) {
        return task === "blockRecognition" ? RequestProfilesBlock
             : task === "decision" ? RequestProfilesDecision
             : task === "layout" ? RequestProfilesLayout
             : task === "rawPageOcr" ? RequestProfilesRawPageOcr
             : task === "fix" ? RequestProfilesFix
             : RequestProfilesPageOcr
    }

    function requestProfileId(task) {
        return task === "blockRecognition" ? Settings.checkRequestProfileId
             : task === "decision" ? Settings.decisionRequestProfileId
             : task === "layout" ? Settings.layoutRequestProfileId
             : task === "rawPageOcr" ? Settings.rawPageOcrRequestProfileId
             : task === "fix" ? Settings.fixRequestProfileId
             : Settings.modelRecipeId
    }

    function setRequestProfileId(task, profileId) {
        if (task === "blockRecognition")
            Settings.checkRequestProfileId = profileId
        else if (task === "decision")
            Settings.decisionRequestProfileId = profileId
        else if (task === "layout")
            Settings.layoutRequestProfileId = profileId
        else if (task === "rawPageOcr")
            Settings.rawPageOcrRequestProfileId = profileId
        else if (task === "fix")
            Settings.fixRequestProfileId = profileId
        else
            Settings.modelRecipeId = profileId
    }
}
