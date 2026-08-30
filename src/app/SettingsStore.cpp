#include "app/SettingsStore.h"

#include <QMetaProperty>

namespace llocr {

// Every setting reset by resetToDefaults(), in the exact order the old manual
// list used. Order matters at the two ends:
//  - baseUrl is reset BEFORE connectionMode, because setConnectionMode(External)
//    restores lastExternalBaseUrl over baseUrl (ADR 26 / §7.9);
//  - lastExternalBaseUrl must be LAST so that restore happens first.
// Window geometry (ui/windowX…State) is intentionally NOT here: it is UI
// state, not a user setting, and the previous reset also left it alone.
const SettingsStore::SettingDefault SettingsStore::kDefaults[] = {
    // UI
    { kLanguage, "language", QVariant(QString::fromUtf8(kDefaultLanguage)) },
    { kThemeMode, "themeMode", QVariant(kDefaultThemeMode) },
    // Connection
    { kBaseUrl, "baseUrl", QVariant(QString::fromUtf8(kDefaultBaseUrl)) },
    { kApiKey, "apiKey", QVariant(QString::fromUtf8(kDefaultApiKey)) },
    { kTimeoutMs, "connectionTimeoutMs", QVariant(kDefaultTimeoutMs) },
    // Model
    { kModelName, "modelName", QVariant(QString::fromUtf8(kDefaultModelName)) },
    { kTemperature, "temperature", QVariant(kDefaultTemperature) },
    { kMaxTokens, "maxTokens", QVariant(kDefaultMaxTokens) },
    { kDryMultiplier, "dryMultiplier", QVariant(kDefaultDryMultiplier) },
    { kDryBase, "dryBase", QVariant(kDefaultDryBase) },
    { kDryAllowedLength, "dryAllowedLength", QVariant(kDefaultDryAllowedLength) },
    { kDryPenaltyLastN, "dryPenaltyLastN", QVariant(kDefaultDryPenaltyLastN) },
    // Parser
    { kParserId, "parserId", QVariant(QString::fromUtf8(kDefaultParserId)) },
    // Connection mode / runtime
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
    { kCheckUpdates, "checkUpdates", QVariant(false) },
    { kAllowNonLoopback, "allowNonLoopback", QVariant(false) },
    // Launch
    { kLaunchPresetId, "launchPresetId", QVariant(QString()) },
    { kLaunchModelPath, "launchModelPath", QVariant(QString()) },
    { kLaunchMmprojPath, "launchMmprojPath", QVariant(QString()) },
    { kLaunchModelAlias, "launchModelAlias", QVariant(QString::fromUtf8(kDefaultModelAlias)) },
    { kLaunchHost, "launchHost", QVariant(QString::fromUtf8(kDefaultHost)) },
    { kLaunchPort, "launchPort", QVariant(kDefaultPort) },
    { kLaunchCtxSize, "launchCtxSize", QVariant(kDefaultCtxSize) },
    { kLaunchGpuLayers, "launchGpuLayers", QVariant(kDefaultGpuLayers) },
    { kLaunchThreads, "launchThreads", QVariant(kDefaultThreads) },
    { kLaunchBatchSize, "launchBatchSize", QVariant(kDefaultBatchSize) },
    { kLaunchParallel, "launchParallel", QVariant(kDefaultParallel) },
    { kLaunchFlashAttn, "launchFlashAttn", QVariant(QString::fromUtf8(kDefaultFlashAttn)) },
    { kLaunchCacheTypeK, "launchCacheTypeK", QVariant(QString()) },
    { kLaunchCacheTypeV, "launchCacheTypeV", QVariant(QString()) },
    { kLaunchNoMmap, "launchNoMmap", QVariant(false) },
    { kLaunchJinja, "launchJinja", QVariant(false) },
    { kLaunchExtraArgs, "launchExtraArgs", QVariant(QString()) },
    // Hugging Face
    { kHfToken, "hfToken", QVariant(QString()) },
    // Saved external endpoint — must stay LAST (see the order comment above).
    { kLastExternalBaseUrl, "lastExternalBaseUrl", QVariant(QString()) },
};

SettingsStore::SettingsStore(QObject *parent) : QObject(parent)
{
    applyStartupMigration();
}

bool SettingsStore::contains(const QString &key) const
{
    return m_settings.contains(key);
}

void SettingsStore::applyStartupMigration()
{
    // §4.4: run only once. After this, runtime/setupVersion exists and the
    // guard below never executes again.
    if (!m_settings.contains(kSetupVersion)) {
        const bool looksConfigured = m_settings.contains(kBaseUrl)
                                  && !m_settings.value(kBaseUrl).toString().isEmpty();
        // Never flip the mode automatically: every pre-existing profile stays
        // in External (ADR 26 / §1.1).
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
    // Table-driven (review 3.5): every resettable key is one row in
    // kDefaults; writing through the Q_PROPERTY keeps the setters' guards,
    // validation and NOTIFY emission, so this behaves exactly like the manual
    // setter list it replaces.
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

QString SettingsStore::connectionMode() const
{
    return m_settings.value(kConnectionMode, QString::fromUtf8(kModeExternal)).toString();
}

ConnectionMode SettingsStore::mode() const
{
    // Single barrier: an unknown/typo stored value falls back to External
    // (ADR 26 — never flip the mode silently). Comparing against the enum
    // everywhere keeps the string↔enum mapping here (review 2.6).
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
    // Only the two known modes are valid; anything else (e.g. a typo) is
    // rejected so an unrecognized value never persists and later silently
    // reads back as External.
    if (mode != QString::fromUtf8(kModeExternal)
        && mode != QString::fromUtf8(kModeManaged)) {
        qWarning("Ignoring invalid connection mode %s", qPrintable(mode));
        return;
    }
    if (connectionMode() == mode)
        return;
    // §4.1/§7.9: entering Managed preserves the external endpoint for the way
    // back; returning to External restores it. provider/baseUrl itself is never
    // clobbered while in Managed (the controller builds the managed URL in
    // memory), so restoring only re-applies what we saved.
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

bool SettingsStore::checkUpdates() const
{
    return m_settings.value(kCheckUpdates, false).toBool();
}

void SettingsStore::setCheckUpdates(bool on)
{
    if (checkUpdates() == on)
        return;
    m_settings.setValue(kCheckUpdates, on);
    emit checkUpdatesChanged();
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

int SettingsStore::launchCtxSize() const
{
    return m_settings.value(kLaunchCtxSize, kDefaultCtxSize).toInt();
}

void SettingsStore::setLaunchCtxSize(int size)
{
    if (launchCtxSize() == size)
        return;
    m_settings.setValue(kLaunchCtxSize, size);
    emit launchCtxSizeChanged();
}

int SettingsStore::launchGpuLayers() const
{
    return m_settings.value(kLaunchGpuLayers, kDefaultGpuLayers).toInt();
}

void SettingsStore::setLaunchGpuLayers(int layers)
{
    if (launchGpuLayers() == layers)
        return;
    m_settings.setValue(kLaunchGpuLayers, layers);
    emit launchGpuLayersChanged();
}

int SettingsStore::launchThreads() const
{
    return m_settings.value(kLaunchThreads, kDefaultThreads).toInt();
}

void SettingsStore::setLaunchThreads(int threads)
{
    if (launchThreads() == threads)
        return;
    m_settings.setValue(kLaunchThreads, threads);
    emit launchThreadsChanged();
}

int SettingsStore::launchBatchSize() const
{
    return m_settings.value(kLaunchBatchSize, kDefaultBatchSize).toInt();
}

void SettingsStore::setLaunchBatchSize(int size)
{
    if (launchBatchSize() == size)
        return;
    m_settings.setValue(kLaunchBatchSize, size);
    emit launchBatchSizeChanged();
}

int SettingsStore::launchParallel() const
{
    return m_settings.value(kLaunchParallel, kDefaultParallel).toInt();
}

void SettingsStore::setLaunchParallel(int parallel)
{
    if (launchParallel() == parallel)
        return;
    m_settings.setValue(kLaunchParallel, parallel);
    emit launchParallelChanged();
}

QString SettingsStore::launchFlashAttn() const
{
    return m_settings.value(kLaunchFlashAttn, QString::fromUtf8(kDefaultFlashAttn)).toString();
}

void SettingsStore::setLaunchFlashAttn(const QString &value)
{
    if (launchFlashAttn() == value)
        return;
    m_settings.setValue(kLaunchFlashAttn, value);
    emit launchFlashAttnChanged();
}

QString SettingsStore::launchCacheTypeK() const
{
    return m_settings.value(kLaunchCacheTypeK).toString();
}

void SettingsStore::setLaunchCacheTypeK(const QString &type)
{
    if (launchCacheTypeK() == type)
        return;
    m_settings.setValue(kLaunchCacheTypeK, type);
    emit launchCacheTypeKChanged();
}

QString SettingsStore::launchCacheTypeV() const
{
    return m_settings.value(kLaunchCacheTypeV).toString();
}

void SettingsStore::setLaunchCacheTypeV(const QString &type)
{
    if (launchCacheTypeV() == type)
        return;
    m_settings.setValue(kLaunchCacheTypeV, type);
    emit launchCacheTypeVChanged();
}

bool SettingsStore::launchNoMmap() const
{
    return m_settings.value(kLaunchNoMmap, false).toBool();
}

void SettingsStore::setLaunchNoMmap(bool on)
{
    if (launchNoMmap() == on)
        return;
    m_settings.setValue(kLaunchNoMmap, on);
    emit launchNoMmapChanged();
}

bool SettingsStore::launchJinja() const
{
    return m_settings.value(kLaunchJinja, false).toBool();
}

void SettingsStore::setLaunchJinja(bool on)
{
    if (launchJinja() == on)
        return;
    m_settings.setValue(kLaunchJinja, on);
    emit launchJinjaChanged();
}

QString SettingsStore::launchExtraArgs() const
{
    return m_settings.value(kLaunchExtraArgs).toString();
}

void SettingsStore::setLaunchExtraArgs(const QString &args)
{
    if (launchExtraArgs() == args)
        return;
    m_settings.setValue(kLaunchExtraArgs, args);
    emit launchExtraArgsChanged();
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
