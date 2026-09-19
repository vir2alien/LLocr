pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    // "ocr" for the recognition model; "check" will be used by the
    // verification-model window once its settings land.
    property string role: "ocr"

    title: role === "check" ? qsTr("Check model settings")
                            : qsTr("OCR model settings")
    width: 560
    height: 680
    modality: Qt.NonModal

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    readonly property bool checkRole: role === "check"

    footer: ToolBar {
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: Theme.spacingSmall

            LLOButton {
                text: qsTr("Restore defaults")
                onClicked: {
                    launchTab.resetValues()
                    if (window.checkRole)
                        checkRequestTab.resetValues()
                    else
                        requestTab.resetValues()
                }
            }
            Item { Layout.fillWidth: true }

            LLOButton {
                text: qsTr("Save")
                onClicked: {
                    launchTab.saveValues()
                    if (window.checkRole)
                        checkRequestTab.saveValues()
                    else
                        requestTab.saveValues()
                    if (!window.checkRole) {
                        const draftId = RequestProfiles.draftProfileId
                        if (draftId.length > 0)
                            Settings.modelRecipeId = draftId
                    }
                    Settings.forceSave()
                    window.close()
                }
            }
            LLOButton {
                text: qsTr("Cancel")
                onClicked: {
                    launchTab.loadValues()
                    if (window.checkRole)
                        checkRequestTab.loadValues()
                    else
                        requestTab.loadValues()
                    window.close()
                }
            }
        }
    }

    function loadValues() {
        launchTab.loadValues()
        if (checkRole)
            checkRequestTab.loadValues()
        else
            requestTab.loadValues()
    }

    onVisibleChanged: {
        if (visible)
            window.loadValues()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            implicitHeight: 28

            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.divider
                }
            }

            component CustomTabButton: TabButton {
                id: tabBtn
                implicitHeight: 28
                padding: 4
                contentItem: Text {
                    text: tabBtn.text
                    font.pointSize: Theme.captionSize
                    color: tabBtn.checked ? Theme.textPrimary : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                }
                background: Rectangle {
                    color: tabBtn.checked ? Theme.surface : Theme.surfaceSunken
                    border.color: Theme.divider
                    border.width: 1
                    Rectangle {
                        visible: tabBtn.checked
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: Theme.surface
                    }
                }
            }

            CustomTabButton { text: qsTr("Location") }
            CustomTabButton { text: qsTr("Launch") }
            CustomTabButton { text: qsTr("Request") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            LocationTab {
                id: locationTab
                role: window.role
            }

            LaunchTab {
                id: launchTab
            }

            // Request settings are role-specific: the OCR model uses the
            // parameter-profile table, the check model — simple fields.
            Item {
                RequestTab {
                    id: requestTab
                    anchors.fill: parent
                    visible: !window.checkRole
                }

                CheckRequestTab {
                    id: checkRequestTab
                    anchors.fill: parent
                    visible: window.checkRole
                }
            }
        }
    }

    // Refresh the model lists when the Location tab (with the installed-model
    // / preset / search lists) becomes active.
    Connections {
        target: tabBar
        function onCurrentIndexChanged() {
            if (tabBar.currentIndex === 0) {
                ModelInstaller.refreshInstalled()
                ModelInstaller.reloadPresets()
                ModelInstaller.rescanRegistry()
            }
        }
    }
}
