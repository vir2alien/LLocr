#pragma once

#include <QObject>
#include <QQmlEngine>

#include "config/SettingsStore.h"

namespace llocr {

class UiController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)

public:
    enum Mode : int {
        System = 0,
        Light = 1,
        Dark = 2,
    };
    Q_ENUM(Mode)

    explicit UiController(SettingsStore &settings, QObject *parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    bool dark() const;

    // The language the app speaks now — the setting, with "system" resolved
    // by the system language.
    QString language() const;

signals:
    void modeChanged();
    void darkChanged();
    void languageChanged();

private:
    void apply() const;

    Mode m_mode = System;
    SettingsStore &m_settings;
};

}  // namespace llocr
