pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    signal edited()

    property bool showHelp: false
    property bool loading: false

    function loadValues() {
        loading = true
        autoCheckBox.checked = Settings.autoCheck
        loading = false
    }

    function saveValues() {
        Settings.autoCheck = autoCheckBox.checked
    }

    component BlockTypeRow: Item {
        id: typeRow

        // Note: the Repeater model is a group-filtered proxy, so there is no
        // `required property int index` here — the source-model row is
        // resolved from the unique `type` (see the TapHandler below).
        required property string type
        required property string name
        required property bool enabled

        Layout.fillWidth: true
        Layout.preferredHeight: Theme.rowHeightLarge

        Rectangle {
            anchors.fill: parent
            radius: Theme.controlRadius
            color: rowHover.hovered ? Theme.selected : "transparent"
        }

        Rectangle {
            id: box
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: Theme.controlRadius
            color: typeRow.enabled ? Theme.accent : Theme.surface
            border.color: typeRow.enabled ? Theme.accent : Theme.border
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "\u2713"
                visible: typeRow.enabled
                font.pointSize: Theme.footnoteSize
                font.bold: true
                color: Theme.dark ? "#1c1c1c" : "#ffffff"
            }
        }

        LLOLabel {
            id: nameLabel
            anchors.left: box.right
            anchors.leftMargin: 10
            anchors.right: hintLabel.visible ? hintLabel.left : parent.right
            anchors.rightMargin: Theme.spacingSmall
            anchors.verticalCenter: parent.verticalCenter
            text: BlockNames.displayName(typeRow.name, typeRow.type)
            color: Theme.textPrimary
            elide: Text.ElideRight
        }

        LLOLabel {
            id: hintLabel
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSmall
            anchors.verticalCenter: parent.verticalCenter
            visible: BlockNames.hint(typeRow.type).length > 0
            width: Math.min(implicitWidth, parent.width / 2)
            text: BlockNames.hint(typeRow.type)
            font.pointSize: Theme.footnoteSize
            color: Theme.helpColor
            elide: Text.ElideRight
        }

        HoverHandler { id: rowHover }
        TapHandler {
            onTapped: {
                // The Repeater model is a group-filtered proxy, so `index`
                // addresses the proxy; resolve the source-model row by the
                // unique block type instead.
                const sourceRow = Verification.blockModel.rowOfType(typeRow.type)
                if (sourceRow >= 0)
                    Verification.blockModel.setEnabled(sourceRow, !typeRow.enabled)
            }
        }

        ToolTip.visible: rowHover.hovered && typeRow.type.length > 0
        ToolTip.delay: 600
        ToolTip.text: typeRow.type
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Automatic checking ------------------------------------------
        LLOCheckBox {
            id: autoCheckBox
            Layout.fillWidth: true
            text: qsTr("Automatic checking")
            font.pointSize: Theme.bodySmallSize
            onToggled: if (!loading) root.edited()
        }
        LLOLabel {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            font.pointSize: Theme.footnoteSize
            color: Theme.helpColor
            wrapMode: Text.WordWrap
            text: qsTr("Runs after recognition. Already checked blocks are skipped.")
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: detailsLabel.implicitHeight
            Layout.topMargin: Theme.spacingSmall

            TapHandler {
                onTapped: root.showHelp = !root.showHelp
            }
            HoverHandler { id: helpHover }

            LLOLabel {
                id: detailsLabel
                text: (root.showHelp ? "\u25be " : "\u25b8 ")
                      + qsTr("How checking works")
                font.pointSize: Theme.footnoteSize
                color: helpHover.hovered ? Theme.textPrimary : Theme.linkColor
            }
        }
        LLOLabel {
            id: detailsLabel2
            Layout.fillWidth: true
            visible: root.showHelp
            font.pointSize: Theme.footnoteSize
            color: Theme.helpColor
            wrapMode: Text.WordWrap
            text: qsTr("When on, verification starts automatically as soon as "
                       + "recognition finishes. The managed runtime switches its "
                       + "loaded model from OCR to the check model; an external "
                       + "runtime just starts checking.")
        }

        // --- Block types --------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingXLarge
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Block types to check")
                font.bold: true
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }
            LLOLabel {
                font.pointSize: Theme.footnoteSize
                color: Theme.helpColor
                text: qsTr("Selected %1 of %2")
                      .arg(Verification.blockModel.enabledCount)
                      .arg(Verification.blockModel.totalCount)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            LLOLabel {
                text: qsTr("Select all")
                font.pointSize: Theme.footnoteSize
                color: selAllHover.hovered ? Theme.textPrimary : Theme.linkColor
                TapHandler {
                    onTapped: Verification.blockModel.setAllEnabled(true)
                }
                HoverHandler {
                    id: selAllHover
                    cursorShape: Qt.PointingHandCursor
                }
            }
            LLOLabel {
                text: "\u00b7"
                font.pointSize: Theme.footnoteSize
                color: Theme.helpColor
            }
            LLOLabel {
                text: qsTr("Deselect all")
                font.pointSize: Theme.footnoteSize
                color: selNoneHover.hovered ? Theme.textPrimary : Theme.linkColor
                TapHandler {
                    onTapped: Verification.blockModel.setAllEnabled(false)
                }
                HoverHandler {
                    id: selNoneHover
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }

        ScrollView {
            id: typesScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spacing
            contentWidth: width
            contentHeight: columnsRow.implicitHeight

            RowLayout {
                id: columnsRow
                width: typesScroll.availableWidth
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: columnsRow.width / 3
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: 0

                    LLOLabel {
                        Layout.fillWidth: true
                        topPadding: Theme.spacingSmall
                        bottomPadding: Theme.spacingSmall
                        text: qsTr("Main content")
                        font.pointSize: Theme.footnoteSize
                        font.bold: true
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }
                    Repeater {
                        model: Verification.blockModelContent
                        delegate: BlockTypeRow {}
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: columnsRow.width / 3
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: 0

                    LLOLabel {
                        Layout.fillWidth: true
                        topPadding: Theme.spacingSmall
                        bottomPadding: Theme.spacingSmall
                        text: qsTr("Captions, footnotes and references")
                        font.pointSize: Theme.footnoteSize
                        font.bold: true
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }
                    Repeater {
                        model: Verification.blockModelCaptions
                        delegate: BlockTypeRow {}
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: columnsRow.width / 3
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: 0

                    LLOLabel {
                        Layout.fillWidth: true
                        topPadding: Theme.spacingSmall
                        bottomPadding: Theme.spacingSmall
                        text: qsTr("Service")
                        font.pointSize: Theme.footnoteSize
                        font.bold: true
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }
                    Repeater {
                        model: Verification.blockModelService
                        delegate: BlockTypeRow {}
                    }
                }
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing
            font.pointSize: Theme.footnoteSize
            color: Theme.helpColor
            wrapMode: Text.WordWrap
            text: qsTr("Blocks without OCR text are not checked.")
        }
    }
}
