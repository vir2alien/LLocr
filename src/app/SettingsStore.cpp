#include "app/SettingsStore.h"

namespace llocr {

SettingsStore::SettingsStore(QObject *parent) : QObject(parent) {}

void SettingsStore::forceSave()
{
    m_settings.sync();
}

void SettingsStore::resetToDefaults()
{
    setLanguage(QString::fromUtf8(kDefaultLanguage));
    setThemeMode(kDefaultThemeMode);
    setBaseUrl(QString::fromUtf8(kDefaultBaseUrl));
    setApiKey(QString::fromUtf8(kDefaultApiKey));
    setConnectionTimeoutMs(kDefaultTimeoutMs);
    setModelName(QString::fromUtf8(kDefaultModelName));
    setTemperature(kDefaultTemperature);
    setMaxTokens(kDefaultMaxTokens);
    setDryMultiplier(kDefaultDryMultiplier);
    setDryBase(kDefaultDryBase);
    setDryAllowedLength(kDefaultDryAllowedLength);
    setDryPenaltyLastN(kDefaultDryPenaltyLastN);
    setParserId(QString::fromUtf8(kDefaultParserId));
}

QString SettingsStore::baseUrl() const
{
    return m_settings.value(kBaseUrl, QString::fromUtf8(kDefaultBaseUrl)).toString();
}

void SettingsStore::setBaseUrl(const QString &url)
{
    if (baseUrl() == url)
        return;
    m_settings.setValue(kBaseUrl, url);
    emit baseUrlChanged();
}

QString SettingsStore::apiKey() const
{
    return m_settings.value(kApiKey, QString::fromUtf8(kDefaultApiKey)).toString();
}

void SettingsStore::setApiKey(const QString &key)
{
    if (apiKey() == key)
        return;
    m_settings.setValue(kApiKey, key);
    emit apiKeyChanged();
}

int SettingsStore::connectionTimeoutMs() const
{
    return m_settings.value(kTimeoutMs, kDefaultTimeoutMs).toInt();
}

void SettingsStore::setConnectionTimeoutMs(int timeOut)
{
    if (connectionTimeoutMs() == timeOut)
        return;
    m_settings.setValue(kTimeoutMs, timeOut);
    emit connectionTimeoutMsChanged();
}

QString SettingsStore::modelName() const
{
    return m_settings.value(kModelName, QString::fromUtf8(kDefaultModelName)).toString();
}

void SettingsStore::setModelName(const QString &modelName)
{
    if (this->modelName() == modelName)
        return;
    m_settings.setValue(kModelName, modelName);
    emit modelNameChanged();
}

double SettingsStore::temperature() const
{
    return m_settings.value(kTemperature, kDefaultTemperature).toDouble();
}

void SettingsStore::setTemperature(double temp)
{
    if (temperature() == temp)
        return;
    m_settings.setValue(kTemperature, temp);
    emit temperatureChanged();
}

int SettingsStore::maxTokens() const
{
    return m_settings.value(kMaxTokens, kDefaultMaxTokens).toInt();
}

void SettingsStore::setMaxTokens(int maxTkns)
{
    if (maxTokens() == maxTkns)
        return;
    m_settings.setValue(kMaxTokens, maxTkns);
    emit maxTokensChanged();
}

double SettingsStore::dryMultiplier() const
{
    return m_settings.value(kDryMultiplier, kDefaultDryMultiplier).toDouble();
}

void SettingsStore::setDryMultiplier(double val)
{
    if (dryMultiplier() == val)
        return;
    m_settings.setValue(kDryMultiplier, val);
    emit dryMultiplierChanged();
}

double SettingsStore::dryBase() const
{
    return m_settings.value(kDryBase, kDefaultDryBase).toDouble();
}

void SettingsStore::setDryBase(double val)
{
    if (dryBase() == val)
        return;
    m_settings.setValue(kDryBase, val);
    emit dryBaseChanged();
}

int SettingsStore::dryAllowedLength() const
{
    return m_settings.value(kDryAllowedLength, kDefaultDryAllowedLength).toInt();
}

void SettingsStore::setDryAllowedLength(int val)
{
    if (dryAllowedLength() == val)
        return;
    m_settings.setValue(kDryAllowedLength, val);
    emit dryAllowedLengthChanged();
}

int SettingsStore::dryPenaltyLastN() const
{
    return m_settings.value(kDryPenaltyLastN, kDefaultDryPenaltyLastN).toInt();
}

void SettingsStore::setDryPenaltyLastN(int val)
{
    if (dryPenaltyLastN() == val)
        return;
    m_settings.setValue(kDryPenaltyLastN, val);
    emit dryPenaltyLastNChanged();
}

QString SettingsStore::parserId() const
{
    return m_settings.value(kParserId, QString::fromUtf8(kDefaultParserId)).toString();
}

void SettingsStore::setParserId(const QString &parserName)
{
    if (parserId() == parserName)
        return;
    m_settings.setValue(kParserId, parserName);
    emit parserIdChanged();
}

int SettingsStore::themeMode() const
{
    return m_settings.value(kThemeMode, kDefaultThemeMode).toInt();
}

void SettingsStore::setThemeMode(int mode)
{
    if (themeMode() == mode)
        return;
    m_settings.setValue(kThemeMode, mode);
    emit themeModeChanged();
}

QString SettingsStore::language() const
{
    return m_settings.value(kLanguage, QString::fromUtf8(kDefaultLanguage)).toString();
}

void SettingsStore::setLanguage(const QString &language)
{
    if (this->language() == language)
        return;
    m_settings.setValue(kLanguage, language);
    emit languageChanged();
}

int SettingsStore::windowX() const
{
    return m_settings.value(kWindowX, 0).toInt();
}

void SettingsStore::setWindowX(int winX)
{
    if (windowX() == winX)
        return;
    m_settings.setValue(kWindowX, winX);
    emit windowXChanged();
}

int SettingsStore::windowY() const
{
    return m_settings.value(kWindowY, 0).toInt();
}

void SettingsStore::setWindowY(int winY)
{
    if (windowY() == winY)
        return;
    m_settings.setValue(kWindowY, winY);
    emit windowYChanged();
}

int SettingsStore::windowWidth() const
{
    return m_settings.value(kWindowWidth, 1360).toInt();
}

void SettingsStore::setWindowWidth(int winWidth)
{
    if (windowWidth() == winWidth)
        return;
    m_settings.setValue(kWindowWidth, winWidth);
    emit windowWidthChanged();
}

int SettingsStore::windowHeight() const
{
    return m_settings.value(kWindowHeight, 820).toInt();
}

void SettingsStore::setWindowHeight(int winHeight)
{
    if (windowHeight() == winHeight)
        return;
    m_settings.setValue(kWindowHeight, winHeight);
    emit windowHeightChanged();
}

int SettingsStore::windowState() const
{
    return m_settings.value(kWindowState, 1).toInt();
}

void SettingsStore::setWindowState(int winState)
{
    m_settings.setValue(kWindowState, winState);
    emit windowStateChanged();
}

}  // namespace llocr
