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

signals:
    void languageApplied();

private:
    void install(const QString &language);

    const SettingsStore &m_settings;
    QTranslator *m_appTranslator = nullptr;
    QTranslator *m_qtTranslator = nullptr;
};

}  // namespace llocr
