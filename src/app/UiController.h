#pragma once

#include <QObject>
#include <QQmlEngine>

#include "app/SettingsStore.h"

namespace llocr {

class UiController : public QObject
{
    Q_OBJECT
    // Registered into the LLocr QML module by hand in main.cpp
    // (qmlRegisterSingletonInstance). The QML_ELEMENT/QML_SINGLETON pair is
    // deliberately absent: with a real module (ADR 100) it would make
    // qmltyperegistrar emit a default-constructor call for a class that has
    // none. It comes back together with the create() factories in stage 4.

    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)

public:
    enum Mode {
        System = 0,
        Light = 1,
        Dark = 2,
    };
    Q_ENUM(Mode)

    explicit UiController(SettingsStore &settings, QObject *parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    bool dark() const;

signals:
    void modeChanged();
    void darkChanged();

private:
    void apply() const;

    Mode m_mode = System;
    SettingsStore &m_settings;
};

}  // namespace llocr
