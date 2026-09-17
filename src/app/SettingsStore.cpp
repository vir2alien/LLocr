#include "app/SettingsStore.h"

#include <QCoreApplication>
#include <QMetaProperty>

namespace llocr {

const SettingsStore::SettingDefault SettingsStore::kDefaults[] = {
    { kLanguage, "language", QVariant(QString::fromUtf8(kDefaultLanguage)) },
    { kThemeMode, "themeMode", QVariant(kDefaultThemeMode) },
    { kBaseUrl, "baseUrl", QVariant(QString::fromUtf8(kDefaultBaseUrl)) },
    { kApiKey, "apiKey", QVariant(QString::fromUtf8(kDefaultApiKey)) },
    { kTimeoutMs, "connectionTimeoutMs", QVariant(kDefaultTimeoutMs) },
    { kModelName, "modelName", QVariant(QString::fromUtf8(kDefaultModelName)) },
    { kModelRecipeId, "modelRecipeId", QVariant(QString::fromUtf8(kDefaultModelRecipeId)) },
    { kParserId, "parserId", QVariant(QString::fromUtf8(kDefaultParserId)) },
    { kSplitPages, "splitPages", QVariant(kDefaultSplitPages) },
    { kKeepPageNumbers, "keepPageNumbers", QVariant(kDefaultKeepPageNumbers) },
    { kPdfLandscape, "pdfLandscape", QVariant(kDefaultPdfLandscape) },
    { kPdfMarginMm, "pdfMarginMm", QVariant(kDefaultPdfMarginMm) },
    { kConnectionMode, "connectionMode", QVariant(QString::fromUtf8(kModeExternal)) },
    { kSetupVersion, "setupVersion", QVariant(0) },  // 0 = re-run first-run wizard
    { kSetupDismissed, "setupDismissed", QVariant(false) },
    { kServerPath, "serverPath", QVariant(QString()) },
    { kServerPathIsManaged, "serverPathIsManaged", QVariant(false) },
    { kRuntimeRootDir, "runtimeRootDir", QVariant(QString()) },
    { kRuntimeModelsDir, "runtimeModelsDir", QVariant(QString()) },
    { kRuntimeBackend, "runtimeBackend", QVariant(QString()) },
    { kInstalledBuild, "installedBuild", QVariant(QString()) },
    { kAutoStart, "autoStart", QVariant(false) },
    { kStartOnDemand, "startOnDemand", QVariant(true) },
    { kStopOnExit, "stopOnExit", QVariant(true) },
    { kAutoRestart, "autoRestart", QVariant(true) },
    { kStartupTimeoutMs, "startupTimeoutMs", QVariant(kDefaultStartupTimeoutMs) },
    { kAllowNonLoopback, "allowNonLoopback", QVariant(false) },
    { kLaunchPresetId, "launchPresetId", QVariant(QString()) },
    { kLaunchProfileId, "launchProfileId", QVariant(QString()) },
    { kLaunchModelPath, "launchModelPath", QVariant(QString()) },
    { kLaunchMmprojPath, "launchMmprojPath", QVariant(QString()) },
    { kLaunchModelAlias, "launchModelAlias", QVariant(QString::fromUtf8(kDefaultModelAlias)) },
    { kLaunchHost, "launchHost", QVariant(QString::fromUtf8(kDefaultHost)) },
    { kLaunchPort, "launchPort", QVariant(kDefaultPort) },
    { kHfToken, "hfToken", QVariant(QString()) },
    { kLastExternalBaseUrl, "lastExternalBaseUrl", QVariant(QString()) },
};

SettingsStore::SettingsStore(QObject *parent) : QObject(parent)
{
    applyStartupMigration();
}

QSettings SettingsStore::makeSettings()
{
    if (QCoreApplication::organizationName().isEmpty()) {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr"));
        QCoreApplication::setApplicationName(QStringLiteral("LLM OCR"));
    }
    return QSettings();
}

bool SettingsStore::contains(const QString &key) const
{
    return m_settings.contains(key);
}

void SettingsStore::applyStartupMigration()
{
    if (!m_settings.contains(kSetupVersion)) {
        const bool looksConfigured = m_settings.contains(kBaseUrl)
                                  && !m_settings.value(kBaseUrl).toString().isEmpty();
        m_settings.setValue(kConnectionMode, QString::fromUtf8(kModeExternal));
        m_settings.setValue(kSetupVersion, looksConfigured ? kCurrentSetupVersion : 0);
    }
}

void SettingsStore::forceSave()
{
    m_settings.sync();
}

void SettingsStore::resetToDefaults()
{
    const QMetaObject *mo = metaObject();
    for (const SettingDefault &entry : kDefaults) {
        const QMetaProperty prop = mo->property(mo->indexOfProperty(entry.property));
        if (!prop.isValid() || !prop.write(this, entry.defaultValue)) {
            qWarning("SettingsStore: resetToDefaults() cannot write property %s",
                     entry.property);
        }
    }
}

const SettingsStore::SettingDefault *SettingsStore::defaults()
{
    return kDefaults;
}

int SettingsStore::defaultsCount()
{
    return int(sizeof(kDefaults) / sizeof(kDefaults[0]));
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

QString SettingsStore::modelRecipeId() const
{
    return m_settings.value(kModelRecipeId, QString::fromUtf8(kDefaultModelRecipeId)).toString();
}

void SettingsStore::setModelRecipeId(const QString &recipeId)
{
    if (modelRecipeId() == recipeId)
        return;
    m_settings.setValue(kModelRecipeId, recipeId);
    emit modelRecipeIdChanged();
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

bool SettingsStore::splitPages() const
{
    return m_settings.value(kSplitPages, kDefaultSplitPages).toBool();
}

void SettingsStore::setSplitPages(bool on)
{
    if (splitPages() == on)
        return;
    m_settings.setValue(kSplitPages, on);
    emit splitPagesChanged();
}

bool SettingsStore::keepPageNumbers() const
{
    return m_settings.value(kKeepPageNumbers, kDefaultKeepPageNumbers).toBool();
}

void SettingsStore::setKeepPageNumbers(bool on)
{
    if (keepPageNumbers() == on)
        return;
    m_settings.setValue(kKeepPageNumbers, on);
    emit keepPageNumbersChanged();
}

bool SettingsStore::pdfLandscape() const
{
    return m_settings.value(kPdfLandscape, kDefaultPdfLandscape).toBool();
}

void SettingsStore::setPdfLandscape(bool on)
{
    if (pdfLandscape() == on)
        return;
    m_settings.setValue(kPdfLandscape, on);
    emit pdfLandscapeChanged();
}

int SettingsStore::pdfMarginMm() const
{
    return m_settings.value(kPdfMarginMm, kDefaultPdfMarginMm).toInt();
}

void SettingsStore::setPdfMarginMm(int mm)
{
    mm = qBound(0, mm, kMaxPdfMarginMm);
    if (pdfMarginMm() == mm)
        return;
    m_settings.setValue(kPdfMarginMm, mm);
    emit pdfMarginMmChanged();
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
    if (windowState() == winState)
        return;
    m_settings.setValue(kWindowState, winState);
    emit windowStateChanged();
}

QString SettingsStore::connectionMode() const
{
    return m_settings.value(kConnectionMode, QString::fromUtf8(kModeExternal)).toString();
}

ConnectionMode SettingsStore::mode() const
{
    return connectionMode() == QString::fromUtf8(kModeManaged)
               ? ConnectionMode::Managed
               : ConnectionMode::External;
}

void SettingsStore::setMode(ConnectionMode mode)
{
    setConnectionMode(mode == ConnectionMode::Managed ? QString::fromUtf8(kModeManaged)
                                                      : QString::fromUtf8(kModeExternal));
}

void SettingsStore::setConnectionMode(const QString &mode)
{
    if (mode != QString::fromUtf8(kModeExternal)
        && mode != QString::fromUtf8(kModeManaged)) {
        qWarning("Ignoring invalid connection mode %s", qPrintable(mode));
        return;
    }
    if (connectionMode() == mode)
        return;
    if (mode == QString::fromUtf8(kModeManaged)) {
        const QString current = baseUrl();
        if (!current.isEmpty())
            setLastExternalBaseUrl(current);
    } else if (mode == QString::fromUtf8(kModeExternal)) {
        const QString saved = lastExternalBaseUrl();
        if (!saved.isEmpty())
            setBaseUrl(saved);
    }
    m_settings.setValue(kConnectionMode, mode);
    emit connectionModeChanged();
}

QString SettingsStore::lastExternalBaseUrl() const
{
    return m_settings.value(kLastExternalBaseUrl).toString();
}

void SettingsStore::setLastExternalBaseUrl(const QString &url)
{
    if (lastExternalBaseUrl() == url)
        return;
    m_settings.setValue(kLastExternalBaseUrl, url);
    emit lastExternalBaseUrlChanged();
}

int SettingsStore::setupVersion() const
{
    return m_settings.value(kSetupVersion, 0).toInt();
}

void SettingsStore::setSetupVersion(int version)
{
    if (setupVersion() == version)
        return;
    m_settings.setValue(kSetupVersion, version);
    emit setupVersionChanged();
}

bool SettingsStore::setupDismissed() const
{
    return m_settings.value(kSetupDismissed, false).toBool();
}

void SettingsStore::setSetupDismissed(bool dismissed)
{
    if (setupDismissed() == dismissed)
        return;
    m_settings.setValue(kSetupDismissed, dismissed);
    emit setupDismissedChanged();
}

QString SettingsStore::serverPath() const
{
    return m_settings.value(kServerPath).toString();
}

void SettingsStore::setServerPath(const QString &path)
{
    if (serverPath() == path)
        return;
    m_settings.setValue(kServerPath, path);
    emit serverPathChanged();
}

bool SettingsStore::serverPathIsManaged() const
{
    return m_settings.value(kServerPathIsManaged, false).toBool();
}

void SettingsStore::setServerPathIsManaged(bool managed)
{
    if (serverPathIsManaged() == managed)
        return;
    m_settings.setValue(kServerPathIsManaged, managed);
    emit serverPathIsManagedChanged();
}

QString SettingsStore::runtimeRootDir() const
{
    return m_settings.value(kRuntimeRootDir).toString();
}

void SettingsStore::setRuntimeRootDir(const QString &dir)
{
    if (runtimeRootDir() == dir)
        return;
    m_settings.setValue(kRuntimeRootDir, dir);
    emit runtimeRootDirChanged();
}

QString SettingsStore::runtimeModelsDir() const
{
    return m_settings.value(kRuntimeModelsDir).toString();
}

void SettingsStore::setRuntimeModelsDir(const QString &dir)
{
    if (runtimeModelsDir() == dir)
        return;
    m_settings.setValue(kRuntimeModelsDir, dir);
    emit runtimeModelsDirChanged();
}

QString SettingsStore::runtimeBackend() const
{
    return m_settings.value(kRuntimeBackend).toString();
}

void SettingsStore::setRuntimeBackend(const QString &backend)
{
    if (runtimeBackend() == backend)
        return;
    m_settings.setValue(kRuntimeBackend, backend);
    emit runtimeBackendChanged();
}

QString SettingsStore::installedBuild() const
{
    return m_settings.value(kInstalledBuild).toString();
}

void SettingsStore::setInstalledBuild(const QString &build)
{
    if (installedBuild() == build)
        return;
    m_settings.setValue(kInstalledBuild, build);
    emit installedBuildChanged();
}

bool SettingsStore::autoStart() const
{
    return m_settings.value(kAutoStart, false).toBool();
}

void SettingsStore::setAutoStart(bool on)
{
    if (autoStart() == on)
        return;
    m_settings.setValue(kAutoStart, on);
    emit autoStartChanged();
}

bool SettingsStore::startOnDemand() const
{
    return m_settings.value(kStartOnDemand, true).toBool();
}

void SettingsStore::setStartOnDemand(bool on)
{
    if (startOnDemand() == on)
        return;
    m_settings.setValue(kStartOnDemand, on);
    emit startOnDemandChanged();
}

bool SettingsStore::stopOnExit() const
{
    return m_settings.value(kStopOnExit, true).toBool();
}

void SettingsStore::setStopOnExit(bool on)
{
    if (stopOnExit() == on)
        return;
    m_settings.setValue(kStopOnExit, on);
    emit stopOnExitChanged();
}

bool SettingsStore::autoRestart() const
{
    return m_settings.value(kAutoRestart, true).toBool();
}

void SettingsStore::setAutoRestart(bool on)
{
    if (autoRestart() == on)
        return;
    m_settings.setValue(kAutoRestart, on);
    emit autoRestartChanged();
}

int SettingsStore::startupTimeoutMs() const
{
    return m_settings.value(kStartupTimeoutMs, kDefaultStartupTimeoutMs).toInt();
}

void SettingsStore::setStartupTimeoutMs(int ms)
{
    if (startupTimeoutMs() == ms)
        return;
    m_settings.setValue(kStartupTimeoutMs, ms);
    emit startupTimeoutMsChanged();
}


bool SettingsStore::allowNonLoopback() const
{
    return m_settings.value(kAllowNonLoopback, false).toBool();
}

void SettingsStore::setAllowNonLoopback(bool on)
{
    if (allowNonLoopback() == on)
        return;
    m_settings.setValue(kAllowNonLoopback, on);
    emit allowNonLoopbackChanged();
}

QString SettingsStore::launchPresetId() const
{
    return m_settings.value(kLaunchPresetId).toString();
}

void SettingsStore::setLaunchPresetId(const QString &id)
{
    if (launchPresetId() == id)
        return;
    m_settings.setValue(kLaunchPresetId, id);
    emit launchPresetIdChanged();
}

QString SettingsStore::launchModelPath() const
{
    return m_settings.value(kLaunchModelPath).toString();
}

void SettingsStore::setLaunchModelPath(const QString &path)
{
    if (launchModelPath() == path)
        return;
    m_settings.setValue(kLaunchModelPath, path);
    emit launchModelPathChanged();
}

QString SettingsStore::launchMmprojPath() const
{
    return m_settings.value(kLaunchMmprojPath).toString();
}

void SettingsStore::setLaunchMmprojPath(const QString &path)
{
    if (launchMmprojPath() == path)
        return;
    m_settings.setValue(kLaunchMmprojPath, path);
    emit launchMmprojPathChanged();
}

QString SettingsStore::launchModelAlias() const
{
    return m_settings.value(kLaunchModelAlias, QString::fromUtf8(kDefaultModelAlias)).toString();
}

void SettingsStore::setLaunchModelAlias(const QString &alias)
{
    if (launchModelAlias() == alias)
        return;
    m_settings.setValue(kLaunchModelAlias, alias);
    emit launchModelAliasChanged();
}

QString SettingsStore::launchHost() const
{
    return m_settings.value(kLaunchHost, QString::fromUtf8(kDefaultHost)).toString();
}

void SettingsStore::setLaunchHost(const QString &host)
{
    if (launchHost() == host)
        return;
    m_settings.setValue(kLaunchHost, host);
    emit launchHostChanged();
}

int SettingsStore::launchPort() const
{
    return m_settings.value(kLaunchPort, kDefaultPort).toInt();
}

void SettingsStore::setLaunchPort(int port)
{
    if (launchPort() == port)
        return;
    m_settings.setValue(kLaunchPort, port);
    emit launchPortChanged();
}

QString SettingsStore::launchProfileId() const
{
    return m_settings.value(kLaunchProfileId).toString();
}

void SettingsStore::setLaunchProfileId(const QString &id)
{
    if (launchProfileId() == id)
        return;
    m_settings.setValue(kLaunchProfileId, id);
    emit launchProfileIdChanged();
}

QString SettingsStore::hfToken() const
{
    return m_settings.value(kHfToken).toString();
}

void SettingsStore::setHfToken(const QString &token)
{
    if (hfToken() == token)
        return;
    m_settings.setValue(kHfToken, token);
    emit hfTokenChanged();
}

}  // namespace llocr
