pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

// One version of a block: the recognized text or the correction. The card is
// the switch — clicking it makes this version the one the document shows, and
// the version in view is the one that wears the accent.
Rectangle {
    id: root

    property string title: ""
    property string badgeText: ""
    property string badgeScore: ""
    property color badgeColor: Theme.textMuted
    // The version the document shows right now.
    property bool active: false
    // A verified version wears the success color, an unjudged one the focus
    // color.
    property bool verified: false
    // The text as HTML: the panel tints the fragments the correction changed.
    property string html: ""
    property string plainText: ""
    property bool mono: false
    // Whether the user may correct the text by hand.
    property bool editable: false
    property bool editing: false

    signal activated()
    signal edited(string text)

    readonly property color accentColor: verified ? Theme.success : Theme.cardAccent

    implicitHeight: layout.implicitHeight + 2 * Theme.spacingSmall
    color: active ? Theme.cardSurface : Theme.cardSurfaceMuted
    border.color: active ? accentColor : Theme.cardBorder
    border.width: active ? 1.5 : 1
    radius: Theme.radius

    // Under the content: the text takes the clicks for itself (to select and
    // copy it), the rest of the card is the switch.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }

    // The edit ends the way the operator ends it: the «Сохранить» button or
    // the same hotkey the panel uses to approve a block. While the edit lasts
    // the card owns the key and the panel's shortcut yields (ADR 201).
    Shortcut {
        sequences: ["Ctrl+Return", "Meta+Return"]
        enabled: root.editing
        onActivated: root.commitEditing()
    }

    function startEditing() {
        root.editing = true
        editArea.text = root.plainText
        editArea.forceActiveFocus()
        editArea.selectAll()
    }
    function commitEditing() {
        if (!root.editing)
            return;
        root.editing = false;
        root.edited(editArea.text)
    }

    function cancelEditing() {
        root.editing = false
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacingSmall
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Rectangle {
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
                radius: 6
                color: root.active ? root.accentColor : "transparent"
                border.color: root.active ? root.accentColor : Theme.border
                border.width: 1.5
            }

            LLOLabel {
                text: root.title
                font.bold: true
                color: root.active ? Theme.textPrimary : Theme.textSecondary
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                Layout.maximumWidth: Math.max(0, root.width - 300)
            }

            // The verdict of this layer: the word and the score the decision
            // model gave it.
            Rectangle {
                visible: root.badgeText.length > 0 || root.badgeScore.length > 0
                Layout.preferredWidth: badgeRow.implicitWidth + 12
                Layout.preferredHeight: 18
                radius: 9
                color: Qt.rgba(root.badgeColor.r, root.badgeColor.g, root.badgeColor.b, 0.15)
                border.color: Qt.rgba(root.badgeColor.r, root.badgeColor.g, root.badgeColor.b, 0.4)
                border.width: 1

                RowLayout {
                    id: badgeRow
                    anchors.centerIn: parent
                    spacing: 4

                    Rectangle {
                        Layout.preferredWidth: 6
                        Layout.preferredHeight: 6
                        radius: 3
                        color: root.badgeColor
                    }
                    LLOLabel {
                        visible: root.badgeText.length > 0
                        text: root.badgeText
                        color: root.badgeColor
                        font.bold: true
                    }
                    LLOLabel {
                        visible: root.badgeScore.length > 0
                        text: root.badgeScore
                        color: root.badgeColor
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }

            // The way into the edit and the way out of it: the button says
            // what the click does.
            LLOButton {
                visible: root.editable && !root.editing
                text: qsTr("Edit")
                subtle: true
                onClicked: root.startEditing()
            }

            LLOButton {
                visible: root.editable && root.editing
                text: qsTr("Save")
                onClicked: root.commitEditing()
            }

            LLOLabel {
                text: root.active ? qsTr("In the document") : qsTr("Click to select")
                color: root.active ? root.accentColor : Theme.textMuted
                font.italic: !root.active
                elide: Text.ElideRight
                Layout.maximumWidth: 170
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Math.max(textView.contentHeight, editArea.contentHeight, 18) + 2 * Theme.spacingSmall, 120)
            color: Theme.cardSurface
            border.color: Theme.cardBorder
            border.width: 1
            radius: Theme.radius
            clip: true

            // The text as it is: the tinted fragments of the diff, or the
            // plain text when the highlighting is off. The field stays
            // selectable — the operator copies a formula from here.
            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                visible: !root.editing
                contentWidth: availableWidth

                TextArea {
                    id: textView
                    width: parent.width
                    readOnly: true
                    textFormat: TextArea.RichText
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    color: root.active ? Theme.textPrimary : Theme.textSecondary
                    font: root.mono ? Theme.mono : Theme.caption
                    background: null
                    topPadding: 0
                    bottomPadding: 0
                    text: root.html
                }
            }

            // The hand-written version of the same text.
            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                visible: root.editing
                contentWidth: availableWidth

                TextArea {
                    id: editArea
                    width: parent.width
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    color: Theme.textPrimary
                    font: root.mono ? Theme.mono : Theme.caption
                    background: null
                    topPadding: 0
                    bottomPadding: 0
                    onEditingFinished: root.commitEditing()
                    Keys.onEscapePressed: root.cancelEditing()
                }
            }
        }
    }
}
