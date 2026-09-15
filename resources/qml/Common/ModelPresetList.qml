import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ListView {
    id: root

    property int rowHeight: 36

    signal installClicked(int index)

    visible: count > 0
    clip: true
    model: ModelInstaller.presetCount
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    delegate: Rectangle {
        id: presetRoot
        required property int index
        property var pInfo: ModelInstaller.presetInfo(index)
        function refreshPresetInfo() {
            pInfo = ModelInstaller.presetInfo(index)
        }
        Connections {
            target: ModelInstaller
            function onInstalledChanged() { presetRoot.refreshPresetInfo() }
            function onPresetsChanged() { presetRoot.refreshPresetInfo() }
        }

        width: root.width
        height: root.rowHeight
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 6
            LLOLabel {
                Layout.preferredWidth: 150
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textPrimary
                text: presetRoot.pInfo.title
            }
            LLOLabel {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: presetRoot.pInfo.repo
            }
            LLOLabel {
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: presetRoot.pInfo.approxVramGb > 0
                      ? qsTr("~%1 GiB VRAM").arg(presetRoot.pInfo.approxVramGb) : ""
            }
            LLOButton {
                text: presetRoot.pInfo.installed ? qsTr("Activate") : qsTr("Install")
                enabled: !ModelInstaller.busy
                onClicked: {
                    if (presetRoot.pInfo.installed) {
                        ModelInstaller.activatePreset(presetRoot.index)
                        return
                    }
                    root.installClicked(presetRoot.index)
                }
            }
        }
    }
}
