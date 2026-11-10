pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// Language and theme pickers. Both the Interface settings tab and the wizard's
// first step show exactly these, and both apply the pick immediately — there
// is no Save step for interface settings.
ColumnLayout {
    id: root

    spacing: Theme.spacingSmall

    signal edited()

    LLOLabel {
        text: qsTr("Language")
        font.pixelSize: Theme.bodySize
        color: Theme.textPrimary
    }
    LLOComboBox {
        id: languageBox
        // The picker is as wide as the theme switch below it: the two controls
        // of the section share one measure. The layout rounds the switch's
        // width to whole pixels, so the picker takes the rounded measure too.
        Layout.preferredWidth: Math.round(themeSwitch.implicitWidth)
        model: [qsTr("System"), "English", "Русский"]
        function syncFromSettings() {
            var langIdx = ["system", "en", "ru"].indexOf(Settings.language)
            currentIndex = langIdx >= 0 ? langIdx : 0
        }
        onModelChanged: syncFromSettings()
        Component.onCompleted: syncFromSettings()
        onActivated: (idx) => {
            Settings.language = ["system", "en", "ru"][idx]
            I18n.setLanguage(Settings.language)
            root.edited()
        }
        Connections {
            target: Settings
            function onLanguageChanged() { languageBox.syncFromSettings() }
        }
    }

    Item { implicitHeight: 6 }

    LLOLabel {
        text: qsTr("Theme")
        font.pixelSize: Theme.bodySize
        color: Theme.textPrimary
    }
    SegmentedControl {
        id: themeSwitch
        options: [
            { label: qsTr("System"), icon: "monitor" },
            { label: qsTr("Light"), icon: "sun" },
            { label: qsTr("Dark"), icon: "moon" }
        ]
        currentIndex: {
            const themeIdx = [UiController.System, UiController.Light,
                              UiController.Dark].indexOf(Settings.themeMode)
            return themeIdx >= 0 ? themeIdx : 0
        }
        onActivated: (idx) => {
            UiController.mode = [UiController.System, UiController.Light,
                                 UiController.Dark][idx]
            root.edited()
        }
    }

    // The window gives the section the whole height; the spacer takes the
    // extra space, so the pickers keep their natural size instead of being
    // spread over the section.
    Item { Layout.fillHeight: true }
}
