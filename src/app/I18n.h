#pragma once

#include <QObject>

class QTranslator;

namespace llocr {

class SettingsStore;

class I18n : public QObject
{
    Q_OBJECT

public:
    explicit I18n(const SettingsStore &settings, QObject *parent = nullptr);

    Q_INVOKABLE void setLanguage(const QString &language);

    void applyInitial();

    // The language the app speaks now: the setting, with "system" resolved
    // by the system language. One rule for the translator and for everything
    // that follows the app's language (the editor's page).
    static QString resolve(const QString &requested);

signals:
    void languageApplied();
    // The catalog did not reach the app: the interface stays in English and
    // says so instead of failing quietly.
    void catalogMissing();

private:
    void install(const QString &language);

private:
    const SettingsStore &m_settings;
    QTranslator *m_appTranslator = nullptr;
    QTranslator *m_qtTranslator = nullptr;
};

}  // namespace llocr
