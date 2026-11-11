#include "app/UiController.h"

#include "app/I18n.h"

#include <QGuiApplication>
#include <QStyleHints>

namespace llocr {

UiController::UiController(SettingsStore &settings, QObject *parent) : QObject(parent), m_settings(settings)
{
    m_mode = static_cast<Mode>(m_settings.themeMode());

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &UiController::darkChanged);
    // The language follows the setting: the editor's page reads it from here.
    connect(&m_settings, &SettingsStore::languageChanged, this, &UiController::languageChanged);

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
    const auto scheme = m_mode == Light ? Qt::ColorScheme::Light : m_mode == Dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Unknown;
    QGuiApplication::styleHints()->setColorScheme(scheme);
}

QString UiController::language() const
{
    return I18n::resolve(m_settings.language());
}

}  // namespace llocr
