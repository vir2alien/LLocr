#include "app/I18n.h"

#include "app/SettingsStore.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace llocr {

namespace {

QString resolveLanguage(const QString &requested)
{
    if (requested == QLatin1String("system")) {
        const QLocale::Language lang = QLocale::system().language();
        return lang == QLocale::Russian ? QStringLiteral("ru") : QStringLiteral("en");
    }
    return requested;
}

}  // namespace

I18n::I18n(const SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
}

void I18n::applyInitial()
{
    install(resolveLanguage(m_settings.language()));
    emit languageApplied();
}

void I18n::setLanguage(const QString &language)
{
    install(resolveLanguage(language));
    emit languageApplied();
}

void I18n::install(const QString &language)
{
    if (m_appTranslator) {
        QCoreApplication::removeTranslator(m_appTranslator);
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        QCoreApplication::removeTranslator(m_qtTranslator);
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }

    if (language != QLatin1String("ru"))
        return;  // "en" (and unknown values): source strings are English.

    m_appTranslator = new QTranslator(this);
    if (m_appTranslator->load(QStringLiteral(":/i18n/llocr_ru.qm"))) {
        QCoreApplication::installTranslator(m_appTranslator);
    } else {
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }

    m_qtTranslator = new QTranslator(this);
    const QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (m_qtTranslator->load(QLocale(QLocale::Russian), QStringLiteral("qtbase"),
                             QStringLiteral("_"), dir)) {
        QCoreApplication::installTranslator(m_qtTranslator);
    } else {
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }
}

}  // namespace llocr
