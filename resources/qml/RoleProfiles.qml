pragma Singleton

import QtQml

import LLocr

// Role-keyed dispatch for the four model-profile roles ("ocr",
// "blockRecognition", "decision", "layout"): every role-conditional binding
// goes through here, so a new role is one edit in one file. The reads stay
// inside the functions, which is what keeps the callers' bindings reactive.
QtObject {
    function title(role) {
        return role === "blockRecognition" ? qsTr("Block OCR model settings")
             : role === "decision" ? qsTr("Decision model settings")
             : role === "layout" ? qsTr("Layout model settings")
             : qsTr("OCR model settings")
    }

    function samplingHint(role) {
        return role === "decision" ? qsTr("Request parameters for the decision endpoint. The set comes from the model profile — edit the values, not the list.")
             : role === "blockRecognition" ? qsTr("Sampling parameters sent with every block OCR request. The set comes from the model profile — edit the values, not the list.")
             : role === "layout" ? qsTr("Sampling parameters sent with every page markup request. The set comes from the model profile — edit the values, not the list.")
             : qsTr("Sampling parameters sent with every recognition request. The set comes from the model profile — edit the values, not the list.")
    }

    function activeTitle(role) {
        return role === "blockRecognition" ? ModelInstaller.checkActiveTitle
             : role === "decision" ? ModelInstaller.decisionActiveTitle
             : role === "layout" ? ModelInstaller.layoutActiveTitle
             : ModelInstaller.activeTitle
    }

    function quantModels(role) {
        return role === "blockRecognition" ? ModelInstaller.checkQuantModels
             : role === "decision" ? ModelInstaller.decisionQuantModels
             : role === "layout" ? ModelInstaller.layoutQuantModels
             : ModelInstaller.quantModels
    }

    function runtimeNote(role) {
        return role === "blockRecognition" ? LaunchProfiles.checkModelRuntimeNote
             : role === "decision" ? LaunchProfiles.decisionModelRuntimeNote
             : role === "layout" ? LaunchProfiles.layoutModelRuntimeNote
             : LaunchProfiles.modelRuntimeNote
    }

    function profiles(role) {
        return role === "blockRecognition" ? RequestProfilesValidate
             : role === "decision" ? RequestProfilesDecision
             : role === "layout" ? RequestProfilesLayout
             : RequestProfilesOcr
    }

    function requestProfileId(role) {
        return role === "blockRecognition" ? Settings.checkRequestProfileId
             : role === "decision" ? Settings.decisionRequestProfileId
             : role === "layout" ? Settings.layoutRequestProfileId
             : Settings.modelRecipeId
    }

    function setRequestProfileId(role, profileId) {
        if (role === "blockRecognition")
            Settings.checkRequestProfileId = profileId
        else if (role === "decision")
            Settings.decisionRequestProfileId = profileId
        else if (role === "layout")
            Settings.layoutRequestProfileId = profileId
        else
            Settings.modelRecipeId = profileId
    }
}
