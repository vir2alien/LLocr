pragma Singleton

import QtQml

import LLocr

// Task-keyed dispatch for the four model tasks ("blockRecognition",
// "decision", "layout", "fix"): every task-conditional binding goes through
// here, so a new task is one row of the table and not a chain of conditions
// (ADR 217).
//
// The texts are values — they are translated when the table is built. The
// columns that read a singleton are functions: the singleton is resolved when
// the task is asked for, not when the table is built, and the binding that
// calls them keeps tracking the settings they read.
QtObject {
    readonly property var tasks: ({
        "blockRecognition": {
            title: qsTr("Block recognition"),
            hint: qsTr("Sampling parameters sent with every block OCR request. The set comes from the model profile — edit the values, not the list."),
            quantModels: function() { return ModelInstaller.checkQuantModels; },
            profiles: function() { return RequestProfilesBlock; },
            requestProfileId: function() { return Settings.modelRecipeId; },
            setRequestProfileId: function(id) { Settings.modelRecipeId = id; }
        },
        "decision": {
            title: qsTr("Verification"),
            hint: qsTr("Request parameters for the decision endpoint. The set comes from the model profile — edit the values, not the list."),
            quantModels: function() { return ModelInstaller.decisionQuantModels; },
            profiles: function() { return RequestProfilesDecision; },
            requestProfileId: function() { return Settings.decisionRequestProfileId; },
            setRequestProfileId: function(id) { Settings.decisionRequestProfileId = id; }
        },
        "layout": {
            title: qsTr("Markup"),
            hint: qsTr("Sampling parameters sent with every page markup request. The set comes from the model profile — edit the values, not the list."),
            quantModels: function() { return ModelInstaller.layoutQuantModels; },
            profiles: function() { return RequestProfilesLayout; },
            requestProfileId: function() { return Settings.layoutRequestProfileId; },
            setRequestProfileId: function(id) { Settings.layoutRequestProfileId = id; }
        },
        "fix": {
            title: qsTr("Block fix"),
            hint: qsTr("Sampling parameters sent with every block fix request. The set comes from the model profile — edit the values, not the list."),
            quantModels: function() { return ModelInstaller.fixQuantModels; },
            profiles: function() { return RequestProfilesFix; },
            requestProfileId: function() { return Settings.fixRequestProfileId; },
            setRequestProfileId: function(id) { Settings.fixRequestProfileId = id; }
        }
    })

    // The row of a task. An unknown id falls back to the block recognition
    // task, the way every ForRole dispatcher treats an unrecognized role.
    function entry(task) {
        return tasks[task] ? tasks[task] : tasks["blockRecognition"]
    }

    function title(task) {
        return entry(task).title
    }

    function samplingHint(task) {
        return entry(task).hint
    }

    function quantModels(task) {
        return entry(task).quantModels()
    }

    function profiles(task) {
        return entry(task).profiles()
    }

    function requestProfileId(task) {
        return entry(task).requestProfileId()
    }

    function setRequestProfileId(task, profileId) {
        entry(task).setRequestProfileId(profileId)
    }

    // Titles of every task riding one missing-model row — applyTaskChoices
    // deduplicates by model+quant and merges the tasks into "tasks".
    function choiceTitles(item) {
        const ids = item.tasks && item.tasks.length > 0 ? item.tasks : [item.task]
        return ids.map(t => title(t)).join(" + ")
    }

    function runtimeNote(task) {
        return LaunchProfiles.modelRuntimeNoteForRole(task)
    }
}
