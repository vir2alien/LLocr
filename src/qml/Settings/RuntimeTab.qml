pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ColumnLayout {
    id: runtimeLayout
    clip: true
    spacing: Theme.spacing

    signal edited()

    // The load fills the fields from the settings: the changes it makes are
    // not the user's, so they must not mark the settings window dirty.
    property bool loading: false

    Layout.maximumHeight: Number.POSITIVE_INFINITY

    function loadValues() {
        runtimeLayout.loading = true
        connectionTimeoutField.text = Settings.connectionTimeoutMs
        responseTimeoutField.text = Settings.responseTimeoutMs
        rtExternal.loadValues();
        rtInternal.loadValues();
        runtimeLayout.loading = false
    }

    function saveValues() {
        Settings.connectionTimeoutMs = parseInt(connectionTimeoutField.text) || Settings.connectionTimeoutMs
        Settings.responseTimeoutMs = parseInt(responseTimeoutField.text) || Settings.responseTimeoutMs
        rtExternal.saveValues();
    }

    // The timeouts are rarely touched: the collapsed header says what they
    // are, and the fields wait below it.
    function timeoutSummary() {
        return qsTr("connect %1 · answer %2")
            .arg(formatDuration(connectionTimeoutField.text))
            .arg(formatDuration(responseTimeoutField.text))
    }

    function formatDuration(ms) {
        const value = parseInt(ms)
        if (!value || value <= 0)
            return "—"
        if (value < 60000)
            return qsTr("%1 s").arg(Math.round(value / 1000))
        return qsTr("%1 min").arg(Math.round(value / 60000))
    }

    // The runtime of a task is chosen in Settings → Models, so both cards
    // stand here: the local llama.cpp and the shared external server.
    RuntimeTabExternal {
        id: rtExternal
        Layout.fillWidth: true
        onEdited: runtimeLayout.edited()
    }

    RuntimeTabInternal {
        id: rtInternal
        Layout.fillWidth: true
        onEdited: runtimeLayout.edited()
    }

    // The timeouts are a block of their own at the bottom: the connection and
    // the models come first, the waiting belongs after them.
    Rectangle {
        id: timeoutsCard

        property bool expanded: false

        Layout.fillWidth: true
        implicitHeight: timeoutsColumn.implicitHeight + Theme.spacing * 2
        color: Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1
        radius: Theme.controlRadius + 2

        ColumnLayout {
            id: timeoutsColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing
            spacing: Theme.spacingSmall

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Icon {
                    name: "chevron-down"
                    rotation: timeoutsCard.expanded ? 0 : 90
                    size: Theme.iconSizeSmall
                    color: Theme.textSecondary
                }
                LLOLabel {
                    text: qsTr("Timeouts")
                    font.pixelSize: Theme.bodySize
                    font.bold: true
                    color: Theme.textPrimary
                }
                Item { Layout.fillWidth: true }
                LLOLabel {
                    text: runtimeLayout.timeoutSummary()
                    font.pixelSize: Theme.footnoteSize
                    color: Theme.helpColor
                }

                HoverHandler {
                    id: timeoutsHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: timeoutsCard.expanded = !timeoutsCard.expanded
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                visible: timeoutsCard.expanded

                ColumnLayout {
                    spacing: Theme.spacingSmall

                    LLOLabel {
                        text: qsTr("Connection timeout (ms)")
                        font.pixelSize: Theme.bodySmallSize
                        color: Theme.textPrimary
                    }
                    TextField {
                        id: connectionTimeoutField
                        Layout.preferredWidth: 160
                        implicitHeight: Theme.controlHeight
                        font.pixelSize: Theme.bodySmallSize
                        selectByMouse: true
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 1000; top: 3600000 }
                        onTextChanged: if (!runtimeLayout.loading) runtimeLayout.edited()
                    }
                }

                ColumnLayout {
                    spacing: Theme.spacingSmall

                    LLOLabel {
                        text: qsTr("Request timeout (ms)")
                        font.pixelSize: Theme.bodySmallSize
                        color: Theme.textPrimary
                    }
                    TextField {
                        id: responseTimeoutField
                        Layout.preferredWidth: 160
                        implicitHeight: Theme.controlHeight
                        font.pixelSize: Theme.bodySmallSize
                        selectByMouse: true
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 1000; top: 3600000 }
                        onTextChanged: if (!runtimeLayout.loading) runtimeLayout.edited()
                    }
                }

                Item { Layout.fillWidth: true }
            }

            LLOLabel {
                Layout.fillWidth: true
                visible: timeoutsCard.expanded
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.footnoteSize
                color: Theme.helpColor
                text: qsTr("The connection timeout is how long to wait for the server to accept a connection; "
                           + "the request timeout is how long to wait for the model's response. "
                           + "Raise the request timeout if long recognitions are cut off.")
            }
        }
    }
}
