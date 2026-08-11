#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace llocr {

class UiController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)

public:
    enum Mode {
        System = 0,
        Light = 1,
        Dark = 2,
    };
    Q_ENUM(Mode)

    explicit UiController(QObject *parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    bool dark() const;

signals:
    void modeChanged();
    void darkChanged();

private:
    void apply() const;

    static Mode modeFromString(const QString& value);
    static QString modeToString(Mode mode);

    static constexpr const char* kMode = "ui/theme";

    Mode m_mode = System;
};

}  // namespace llocr
