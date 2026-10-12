pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Controls

import LLocr

Item
{
    property Window window

    // Suppress save-timer arming while saved geometry is being restored at
    // startup, so it doesn't overwrite the "never positioned" sentinel.
    property bool _restoring: true

    // The last geometry seen in the Windowed state. While maximized/fullscreen
    // the live width/height are the blown-up ones, so the save path falls back
    // to this — without it a first session that goes straight to maximized
    // never persists a size, and the next launch loses the maximized state too.
    property rect lastWindowed: Qt.rect(0, 0, 0, 0)

    Component.onCompleted:
    {
        lastWindowed = Qt.rect(window.x, window.y, window.width, window.height)
        if (Settings.windowWidth && Settings.windowHeight)
        {
            var pos = visiblePosition(Settings.windowX, Settings.windowY,
                                      Settings.windowWidth, Settings.windowHeight);
            window.x = pos.x;
            window.y = pos.y;
            window.width = Settings.windowWidth;
            window.height = Settings.windowHeight;
            window.visibility = Settings.windowState;
        }
        _restoring = false
    }

    Connections
    {
        target: window
        function onXChanged(x) { trackWindowed(); if (_restoring) return; saveSettingsTimer.restart() }
        function onYChanged(y) { trackWindowed(); if (_restoring) return; saveSettingsTimer.restart() }
        function onWidthChanged() { trackWindowed(); if (_restoring) return; saveSettingsTimer.restart() }
        function onHeightChanged() { trackWindowed(); if (_restoring) return; saveSettingsTimer.restart() }
        function onVisibilityChanged() { trackWindowed(); if (_restoring) return; saveSettingsTimer.restart() }
    }

    Timer
    {
        id: saveSettingsTimer
        interval: 1000
        repeat: false
        onTriggered: saveSettings()
    }

    function visiblePosition(x, y, w, h) {
        const screens = Application.screens;
        if (screens.length === 0)
            return { x: x, y: y }; // nothing to validate against

        const band = Math.min(48, h);

        for (var i = 0; i < screens.length; i++) {
            const s = screens[i];
            if (overlaps(x, y, w, band, s.virtualX, s.virtualY, s.width, s.height))
                return { x: x, y: y };
        }

        const cx = x + w / 2;
        const cy = y + h / 2;
        let pick = screens[0];
        let pickD = distToRect(cx, cy, pick.virtualX, pick.virtualY,
                               pick.width, pick.height);
        for (var j = 1; j < screens.length; j++) {
            const s = screens[j];
            const d = distToRect(cx, cy, s.virtualX, s.virtualY, s.width, s.height);
            if (d < pickD) {
                pickD = d;
                pick = s;
            }
        }
        const grab = 60;
        const nx = clamp(cx - w / 2, pick.virtualX - w + grab,
                         pick.virtualX + pick.width - grab);
        const ny = clamp(cy - h / 2, pick.virtualY - band + grab,
                         pick.virtualY + pick.height - grab);
        console.log("WindowSettings: saved position (" + x + "," + y + ") is off-screen; "
                    + "repositioning to (" + nx + "," + ny + ")");
        return { x: nx, y: ny };
    }

    function overlaps(x, y, w, h, rx, ry, rw, rh) {
        return x < rx + rw && x + w > rx && y < ry + rh && y + h > ry;
    }

    function clamp(v, lo, hi) {
        return Math.max(lo, Math.min(hi, v));
    }

    function distToRect(px, py, rx, ry, rw, rh) {
        const dx = Math.max(0, Math.max(rx - px, px - (rx + rw)));
        const dy = Math.max(0, Math.max(ry - py, py - (ry + rh)));
        return dx * dx + dy * dy;
    }

    function trackWindowed() {
        if (window.visibility === ApplicationWindow.Windowed)
            lastWindowed = Qt.rect(window.x, window.y, window.width, window.height)
    }

    function saveSettings() {
        if (window.visibility === ApplicationWindow.Windowed) {
            lastWindowed = Qt.rect(window.x, window.y, window.width, window.height)
            Settings.windowX = window.x;
            Settings.windowY = window.y;
            Settings.windowWidth = window.width;
            Settings.windowHeight = window.height;
            Settings.windowState = window.visibility;
        } else if (window.visibility === ApplicationWindow.FullScreen
                   || window.visibility === ApplicationWindow.Maximized) {
            if (lastWindowed.width > 0 && lastWindowed.height > 0) {
                Settings.windowX = lastWindowed.x;
                Settings.windowY = lastWindowed.y;
                Settings.windowWidth = lastWindowed.width;
                Settings.windowHeight = lastWindowed.height;
            }
            Settings.windowState = window.visibility;
        }
    }
}