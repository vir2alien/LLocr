#include "app/UiController.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

namespace llocr {

UiController::UiController(QObject *parent) : QObject(parent)
{
    QSettings settings;
    m_mode = modeFromString(settings.value(kMode).toString());

    connect(QGuiApplication::styleHints(),
            &QStyleHints::colorSchemeChanged,
            this,
            &UiController::darkChanged);

    apply();
}

void UiController::setMode(Mode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;
    apply();

    QSettings settings;
    settings.setValue(kMode, modeToString(m_mode));
    settings.sync();

    emit modeChanged();
    emit darkChanged();
}

bool UiController::dark() const
{
    switch (m_mode) {
    case Light:
        return false;
    case Dark:
        return true;
    case System:
        break;
    }
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void UiController::apply() const
{
    const auto scheme = m_mode == Light ? Qt::ColorScheme::Light
                        : m_mode == Dark ? Qt::ColorScheme::Dark
                                         : Qt::ColorScheme::Unknown;
    QGuiApplication::styleHints()->setColorScheme(scheme);
}

UiController::Mode UiController::modeFromString(const QString &value)
{
    if (value == QLatin1String("light"))
        return Light;
    if (value == QLatin1String("dark"))
        return Dark;
    return System;
}

QString UiController::modeToString(Mode mode)
{
    switch (mode) {
    case Light:
        return QStringLiteral("light");
    case Dark:
        return QStringLiteral("dark");
    case System:
        break;
    }
    return QStringLiteral("system");
}

}  // namespace llocr
