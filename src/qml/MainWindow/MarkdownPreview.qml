pragma ComponentBehavior: Bound

import QtQuick
import QtWebEngine

import LLocr
import "../Common"

Item {
    id: root

    property string markdown: ""
    property bool dark: false
    property color bgColor: "white"
    property color fgColor: "black"

    property bool _pageReady: false
    property string _loadError: ""

    function _push() {
        if (!_pageReady) return
        var js = "render(" + JSON.stringify(root.markdown) + ", "
               + JSON.stringify({ dark: root.dark, bg: root.bgColor, fg: root.fgColor }) + ");"
        web.runJavaScript(js)
    }

    Timer {
        id: debounce
        interval: 250
        onTriggered: root._push()
    }

    onMarkdownChanged: debounce.restart()
    onDarkChanged: _push()
    onBgColorChanged: _push()
    onFgColorChanged: _push()

    // A failed preview.html load used to leave a permanently blank pane; the
    // banner is cleared by the next successful load (a retry via url change).
    LLOLabel {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingWindow
        visible: root._loadError !== ""
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pointSize: Theme.bodySmallSize
        color: Theme.textSecondary
        text: root._loadError
    }

    WebEngineView {
        id: web
        anchors.fill: parent
        backgroundColor: root.bgColor
        url: "qrc:/preview/preview.html"

        settings.localContentCanAccessFileUrls: false
        settings.localContentCanAccessRemoteUrls: false

        onContextMenuRequested: (request) => request.accepted = true
        onNavigationRequested: (request) => {
            if (request.navigationType !== WebEngineNavigationRequest.TypedNavigation
                && !request.url.toString().startsWith("qrc:/preview/"))
                request.action = WebEngineNavigationRequest.IgnoreRequest
        }

        onLoadingChanged: (info) => {
            if (info.status === WebEngineView.LoadSucceededStatus) {
                root._pageReady = true
                root._loadError = ""
                root._push()
            } else if (info.status === WebEngineView.LoadFailedStatus) {
                root._pageReady = false
                root._loadError = qsTr("Failed to load the preview page (%1).").arg(info.errorString)
            }
        }
    }
}
