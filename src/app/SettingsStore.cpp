#include "app/SettingsStore.h"

namespace llocr {

SettingsStore::SettingsStore(QObject *parent) : QObject(parent) {}

void SettingsStore::forceSave()
{
    m_settings.sync();
}

QString SettingsStore::baseUrl() const
{
    return m_settings.value(kBaseUrl, QStringLiteral("http://localhost:8080")).toString();
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
    return m_settings.value(kApiKey, QString{}).toString();
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
    return m_settings.value(kTimeoutMs, 120000).toInt();
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
    return m_settings.value(kModelName, "Unlimited-OCR").toString();
}

void SettingsStore::setModelName(const QString &modelName)
{
    m_settings.setValue(kModelName, modelName);
    emit modelNameChanged();
}

double SettingsStore::temperature() const
{
    return m_settings.value(kTemperature, 0.0).toDouble();
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
    return m_settings.value(kMaxTokens, 32768).toInt();
}

void SettingsStore::setMaxTokens(int maxTkns)
{
    if (maxTokens() == maxTkns)
        return;
    m_settings.setValue(kMaxTokens, maxTkns);
    emit maxTokensChanged();
}

QString SettingsStore::parserId() const
{
    return m_settings.value(kParserId, "raw").toString();
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
    return m_settings.value(kThemeMode, 0).toInt();
}

void SettingsStore::setThemeMode(int mode)
{
    if (themeMode() == mode)
        return;
    m_settings.setValue(kThemeMode, mode);
    emit themeModeChanged();
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
