#!/bin/sh
# What does Qt WebEngine cost, and when?
#
# Answers the question behind the Markdown-preview split (docs/optimization-plan,
# S1) with numbers instead of an opinion. It builds three minimal apps against
# the same Qt:
#
#   m_quick    plain Qt Quick window                              - the floor
#   m_webinit  QtWebEngineQuick::initialize(), no view ever built - what LLocr
#              sits at between startup and the first preview toggle
#   m_web      a live WebEngineView                               - the cost the
#              preview actually incurs
#
# Only the third number matters. If m_webinit is close to m_quick, then
# initialize() is free at steady state, the existing lazy `Loader` already
# defers the whole cost, and moving the preview into another process would save
# nothing while adding a second application's floor.
#
# The footprint is read from the *whole process tree*: WebEngine spawns helper
# processes, so looking at the main process alone understates the cost.
#
# Usage: scripts/measure-webengine-cost.sh
#        LLOCR_QT_PREFIX=/path/to/Qt/6.x.y/macos scripts/measure-webengine-cost.sh
set -eu

src=$(mktemp -d "${TMPDIR:-/tmp}/llocr-webeng.XXXXXX")
build="$src/build"
qt=${LLOCR_QT_PREFIX:-/Users/gladskikh/Qt/6.10.3/macos}

cat >"$src/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.21)
project(webeng_measure LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick WebEngineQuick WebEngineCore)
add_executable(m_quick m_quick.cpp)
target_link_libraries(m_quick PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
add_executable(m_webinit m_webinit.cpp)
target_link_libraries(m_webinit PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::WebEngineQuick Qt6::WebEngineCore)
add_executable(m_web m_web.cpp)
target_link_libraries(m_web PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::WebEngineQuick Qt6::WebEngineCore)
CMAKE

cat >"$src/m_quick.cpp" <<'CPP'
#include <QGuiApplication>
#include <QQmlApplicationEngine>
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
CPP

cat >"$src/m_webinit.cpp" <<'CPP'
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
int main(int argc, char *argv[])
{
    // The browser process is initialised, but no view is ever created: the same
    // state LLocr sits in between startup and the first preview toggle.
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
CPP

cat >"$src/m_web.cpp" <<'CPP'
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
int main(int argc, char *argv[])
{
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
CPP

cat >"$src/Window.qml" <<'QML'
import QtQuick
Window {
    width: 1280; height: 800; visible: true
}
QML

cat >"$src/WebWindow.qml" <<'QML'
import QtQuick
import QtWebEngine
Window {
    width: 1280; height: 800; visible: true
    WebEngineView { anchors.fill: parent; url: "about:blank" }
}
QML

cmake -S "$src" -B "$build" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$qt" >/dev/null
cmake --build "$build" -j 8 >/dev/null

measure() {
    "$1" "$2" >/dev/null 2>&1 &
    pid=$!
    sleep 6
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "$(basename "$1"): exited early"
        return 1
    fi
    # NB: the banner line also contains the word "Footprint:", so anchor on the
    # phys_footprint: line instead.
    main=$(footprint -p "$pid" 2>/dev/null | awk '/phys_footprint:/{print $2; exit}')
    ksum=0
    for k in $(pgrep -P "$pid" 2>/dev/null); do
        f=$(footprint -p "$k" 2>/dev/null | awk '/phys_footprint:/{print $2; exit}')
        ksum=$(( ksum + ${f%MB} ))
    done
    mv=${main%MB}
    echo "$(basename "$1"): main=$main  helpers=${ksum} MB  tree=$(( mv + ksum )) MB"
    kill "$pid" 2>/dev/null || true
    sleep 1
    pkill -f QtWebEngineProcess 2>/dev/null || true
}

echo "Qt: $qt"
measure "$build/m_quick" "$src/Window.qml"
measure "$build/m_webinit" "$src/Window.qml"
measure "$build/m_web" "$src/WebWindow.qml"
