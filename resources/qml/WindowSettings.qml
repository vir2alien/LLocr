import QtQuick
import QtQuick.Window
import QtQuick.Controls

import LLocr

Item
{
    property Window window

    Component.onCompleted:
    {
        if (Settings.windowWidth && Settings.windowHeight)
        {
            window.x = Settings.windowX;
            window.y = Settings.windowY;
            window.width = Settings.windowWidth;
            window.height = Settings.windowHeight;
            window.visibility = Settings.windowState;
        }
    }

    Connections
    {
        target: window
        function onXChanged(x) { saveSettingsTimer.restart() }
        function onYChanged(y) { saveSettingsTimer.restart() }
        function onWidthChanged() { saveSettingsTimer.restart() }
        function onHeightChanged() { saveSettingsTimer.restart() }
        function onVisibilityChanged() { saveSettingsTimer.restart() }
    }

    Timer
    {
        id: saveSettingsTimer
        interval: 1000
        repeat: false
        onTriggered: saveSettings()
    }

    function saveSettings() {
        switch(window.visibility) {
        case ApplicationWindow.Windowed:
            Settings.windowX = window.x;
            Settings.windowY = window.y;
            Settings.windowWidth = window.width;
            Settings.windowHeight = window.height;
            Settings.windowState = window.visibility;
            break;
        case ApplicationWindow.FullScreen:
            Settings.windowState = window.visibility;
            break;
        case ApplicationWindow.Maximized:
            Settings.windowState = window.visibility;
            break;
        }
    }
}
