pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    title: qsTr("Verification settings")
    width: 680
    height: 560
    modality: Qt.NonModal

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

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
                    Verification.resetToDefaults()
                    loadAll()
                }
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                text: qsTr("Save")
                onClicked: {
                    saveAll()
                    Verification.save()
                    window.close()
                }
            }
            LLOButton {
                text: qsTr("Cancel")
                onClicked: {
                    Verification.loadValues()
                    window.close()
                }
            }
        }
    }

    function loadAll() {
        // Reload the draft from the store (built-in + user overrides).
        Verification.loadValues()
        systemTab.loadValues()
        blocksTab.loadValues()
    }

    function saveAll() {
        systemTab.saveValues()
        blocksTab.saveValues()
    }

    onVisibleChanged: {
        if (visible)
            window.loadAll()
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

            CustomTabButton { text: qsTr("Blocks") }
            CustomTabButton { text: qsTr("System prompt") }
            CustomTabButton { text: qsTr("Block prompts") }
        }//TabBar

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            VerificationBlocksTab {
                id: blocksTab
            }

            VerificationSystemTab {
                id: systemTab
            }

            VerificationPromptsTab {
                id: promptsTab
            }
        }
    }//ColumnLayout
}