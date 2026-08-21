#include "app/UiController.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

namespace llocr {

UiController::UiController(SettingsStore &settings, QObject *parent) : m_settings(settings), QObject(parent)
{
    m_mode = static_cast<Mode>(m_settings.themeMode());

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

    m_settings.setThemeMode(m_mode);

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

}  // namespace llocr
