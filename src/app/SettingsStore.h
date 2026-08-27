#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

#include "runtime/ConnectionMode.h"

namespace llocr {

class SettingsStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(int connectionTimeoutMs READ connectionTimeoutMs WRITE setConnectionTimeoutMs NOTIFY connectionTimeoutMsChanged)
    Q_PROPERTY(QString modelName READ modelName WRITE setModelName NOTIFY modelNameChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(int maxTokens READ maxTokens WRITE setMaxTokens NOTIFY maxTokensChanged)
    Q_PROPERTY(double dryMultiplier READ dryMultiplier WRITE setDryMultiplier NOTIFY dryMultiplierChanged)
    Q_PROPERTY(double dryBase READ dryBase WRITE setDryBase NOTIFY dryBaseChanged)
    Q_PROPERTY(int dryAllowedLength READ dryAllowedLength WRITE setDryAllowedLength NOTIFY dryAllowedLengthChanged)
    Q_PROPERTY(int dryPenaltyLastN READ dryPenaltyLastN WRITE setDryPenaltyLastN NOTIFY dryPenaltyLastNChanged)
    Q_PROPERTY(QString parserId READ parserId WRITE setParserId NOTIFY parserIdChanged)
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int windowX READ windowX WRITE setWindowX NOTIFY windowXChanged)
    Q_PROPERTY(int windowY READ windowY WRITE setWindowY NOTIFY windowYChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY windowWidthChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowHeightChanged)
    Q_PROPERTY(int windowState READ windowState WRITE setWindowState NOTIFY windowStateChanged)

    // --- Connection mode / Managed runtime ---
    Q_PROPERTY(QString connectionMode READ connectionMode WRITE setConnectionMode NOTIFY connectionModeChanged)
    Q_PROPERTY(QString lastExternalBaseUrl READ lastExternalBaseUrl WRITE setLastExternalBaseUrl NOTIFY lastExternalBaseUrlChanged)

    // --- Runtime (setup) ---
    Q_PROPERTY(int setupVersion READ setupVersion WRITE setSetupVersion NOTIFY setupVersionChanged)
    Q_PROPERTY(bool setupDismissed READ setupDismissed WRITE setSetupDismissed NOTIFY setupDismissedChanged)
    Q_PROPERTY(QString serverPath READ serverPath WRITE setServerPath NOTIFY serverPathChanged)
    Q_PROPERTY(bool serverPathIsManaged READ serverPathIsManaged WRITE setServerPathIsManaged NOTIFY serverPathIsManagedChanged)
    Q_PROPERTY(QString runtimeRootDir READ runtimeRootDir WRITE setRuntimeRootDir NOTIFY runtimeRootDirChanged)
    Q_PROPERTY(QString runtimeModelsDir READ runtimeModelsDir WRITE setRuntimeModelsDir NOTIFY runtimeModelsDirChanged)
    Q_PROPERTY(QString runtimeBackend READ runtimeBackend WRITE setRuntimeBackend NOTIFY runtimeBackendChanged)
    Q_PROPERTY(QString installedBuild READ installedBuild WRITE setInstalledBuild NOTIFY installedBuildChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    Q_PROPERTY(bool startOnDemand READ startOnDemand WRITE setStartOnDemand NOTIFY startOnDemandChanged)
    Q_PROPERTY(bool stopOnExit READ stopOnExit WRITE setStopOnExit NOTIFY stopOnExitChanged)
    Q_PROPERTY(bool autoRestart READ autoRestart WRITE setAutoRestart NOTIFY autoRestartChanged)
    Q_PROPERTY(int startupTimeoutMs READ startupTimeoutMs WRITE setStartupTimeoutMs NOTIFY startupTimeoutMsChanged)
    Q_PROPERTY(bool checkUpdates READ checkUpdates WRITE setCheckUpdates NOTIFY checkUpdatesChanged)
    Q_PROPERTY(bool allowNonLoopback READ allowNonLoopback WRITE setAllowNonLoopback NOTIFY allowNonLoopbackChanged)

    // --- Launch (managed server argv) ---
    Q_PROPERTY(QString launchPresetId READ launchPresetId WRITE setLaunchPresetId NOTIFY launchPresetIdChanged)
    Q_PROPERTY(QString launchModelPath READ launchModelPath WRITE setLaunchModelPath NOTIFY launchModelPathChanged)
    Q_PROPERTY(QString launchMmprojPath READ launchMmprojPath WRITE setLaunchMmprojPath NOTIFY launchMmprojPathChanged)
    Q_PROPERTY(QString launchModelAlias READ launchModelAlias WRITE setLaunchModelAlias NOTIFY launchModelAliasChanged)
    Q_PROPERTY(QString launchHost READ launchHost WRITE setLaunchHost NOTIFY launchHostChanged)
    Q_PROPERTY(int launchPort READ launchPort WRITE setLaunchPort NOTIFY launchPortChanged)
    Q_PROPERTY(int launchCtxSize READ launchCtxSize WRITE setLaunchCtxSize NOTIFY launchCtxSizeChanged)
    Q_PROPERTY(int launchGpuLayers READ launchGpuLayers WRITE setLaunchGpuLayers NOTIFY launchGpuLayersChanged)
    Q_PROPERTY(int launchThreads READ launchThreads WRITE setLaunchThreads NOTIFY launchThreadsChanged)
    Q_PROPERTY(int launchBatchSize READ launchBatchSize WRITE setLaunchBatchSize NOTIFY launchBatchSizeChanged)
    Q_PROPERTY(int launchParallel READ launchParallel WRITE setLaunchParallel NOTIFY launchParallelChanged)
    Q_PROPERTY(QString launchFlashAttn READ launchFlashAttn WRITE setLaunchFlashAttn NOTIFY launchFlashAttnChanged)
    Q_PROPERTY(QString launchCacheTypeK READ launchCacheTypeK WRITE setLaunchCacheTypeK NOTIFY launchCacheTypeKChanged)
    Q_PROPERTY(QString launchCacheTypeV READ launchCacheTypeV WRITE setLaunchCacheTypeV NOTIFY launchCacheTypeVChanged)
    Q_PROPERTY(bool launchNoMmap READ launchNoMmap WRITE setLaunchNoMmap NOTIFY launchNoMmapChanged)
    Q_PROPERTY(bool launchJinja READ launchJinja WRITE setLaunchJinja NOTIFY launchJinjaChanged)
    Q_PROPERTY(QString launchExtraArgs READ launchExtraArgs WRITE setLaunchExtraArgs NOTIFY launchExtraArgsChanged)

    // --- Hugging Face ---
    Q_PROPERTY(QString hfToken READ hfToken WRITE setHfToken NOTIFY hfTokenChanged)

public:
    explicit SettingsStore(QObject *parent = nullptr);
    // Defaults
    static constexpr const char *kDefaultBaseUrl = "http://localhost:8080";
    static constexpr const char *kDefaultApiKey = "";
    static constexpr int kDefaultTimeoutMs = 120000;
    static constexpr const char *kDefaultModelName = "Unlimited-OCR";
    static constexpr double kDefaultTemperature = 0.0;
    static constexpr int kDefaultMaxTokens = 8192;
    static constexpr double kDefaultDryMultiplier = 0.8;
    static constexpr double kDefaultDryBase = 1.75;
    static constexpr int kDefaultDryAllowedLength = 35;
    static constexpr int kDefaultDryPenaltyLastN = 2048;
    static constexpr const char *kDefaultParserId = "det_tokens";
    static constexpr int kDefaultThemeMode = 0; // System
    static constexpr const char *kDefaultLanguage = "system";

    // Runtime / launch defaults (Stage A, §4.1).
    static constexpr const char *kDefaultModelAlias = "llocr-local";
    static constexpr const char *kDefaultHost = "127.0.0.1";
    static constexpr int kDefaultPort = 0;        // 0 = auto-pick
    static constexpr int kDefaultCtxSize = 8192;
    static constexpr int kDefaultGpuLayers = -1;   // -1 = default (do not set)
    static constexpr int kDefaultThreads = 0;      // 0 = do not pass
    static constexpr int kDefaultBatchSize = 0;    // 0 = do not pass
    static constexpr int kDefaultParallel = 1;
    static constexpr const char *kDefaultFlashAttn = "off";
    static constexpr int kDefaultStartupTimeoutMs = 180000;

    Q_INVOKABLE void forceSave();
    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE bool contains(const QString &key) const;

    /// Applies §4.4 migration: existing profiles must not see the first-run
    /// wizard, and `provider/mode` must never be flipped automatically.
    /// Runs once per construction (guarded by the presence of
    /// `runtime/setupVersion`).
    void applyStartupMigration();

    // Connection
    QString baseUrl() const;
    void setBaseUrl(const QString &url);
    QString apiKey() const;
    void setApiKey(const QString &key);
    int connectionTimeoutMs() const;
    void setConnectionTimeoutMs(int timeOut);

    // Model
    QString modelName() const;
    void setModelName(const QString &modelName);
    double temperature() const;
    void setTemperature(double temp);
    int maxTokens() const;
    void setMaxTokens(int maxTkns);
    double dryMultiplier() const;
    void setDryMultiplier(double val);
    double dryBase() const;
    void setDryBase(double val);
    int dryAllowedLength() const;
    void setDryAllowedLength(int val);
    int dryPenaltyLastN() const;
    void setDryPenaltyLastN(int val);

    // Parser
    QString parserId() const;
    void setParserId(const QString &parserName);

    // UI
    int themeMode() const;
    void setThemeMode(int mode);
    QString language() const;
    void setLanguage(const QString &language);
    int windowX() const;
    void setWindowX(int winX);
    int windowY() const;
    void setWindowY(int winY);
    int windowWidth() const;
    void setWindowWidth(int winWidth);
    int windowHeight() const;
    void setWindowHeight(int winHeight);
    int windowState() const;
    void setWindowState(int winState);

    // --- Connection mode ---
    // String form is QML-facing (persisted in QSettings; ADR 26). The typed
    // mode()/setMode() are the canonical C++ barrier — all string↔enum mapping
    // lives here (review 2.6).
    QString connectionMode() const;
    void setConnectionMode(const QString &mode);
    ConnectionMode mode() const;
    void setMode(ConnectionMode mode);
    QString lastExternalBaseUrl() const;
    void setLastExternalBaseUrl(const QString &url);

    // --- Runtime (setup) ---
    int setupVersion() const;
    void setSetupVersion(int version);
    bool setupDismissed() const;
    void setSetupDismissed(bool dismissed);
    QString serverPath() const;
    void setServerPath(const QString &path);
    bool serverPathIsManaged() const;
    void setServerPathIsManaged(bool managed);
    QString runtimeRootDir() const;
    void setRuntimeRootDir(const QString &dir);
    QString runtimeModelsDir() const;
    void setRuntimeModelsDir(const QString &dir);
    QString runtimeBackend() const;
    void setRuntimeBackend(const QString &backend);
    QString installedBuild() const;
    void setInstalledBuild(const QString &build);
    bool autoStart() const;
    void setAutoStart(bool on);
    bool startOnDemand() const;
    void setStartOnDemand(bool on);
    bool stopOnExit() const;
    void setStopOnExit(bool on);
    bool autoRestart() const;
    void setAutoRestart(bool on);
    int startupTimeoutMs() const;
    void setStartupTimeoutMs(int ms);
    bool checkUpdates() const;
    void setCheckUpdates(bool on);
    bool allowNonLoopback() const;
    void setAllowNonLoopback(bool on);

    // --- Launch ---
    QString launchPresetId() const;
    void setLaunchPresetId(const QString &id);
    QString launchModelPath() const;
    void setLaunchModelPath(const QString &path);
    QString launchMmprojPath() const;
    void setLaunchMmprojPath(const QString &path);
    QString launchModelAlias() const;
    void setLaunchModelAlias(const QString &alias);
    QString launchHost() const;
    void setLaunchHost(const QString &host);
    int launchPort() const;
    void setLaunchPort(int port);
    int launchCtxSize() const;
    void setLaunchCtxSize(int size);
    int launchGpuLayers() const;
    void setLaunchGpuLayers(int layers);
    int launchThreads() const;
    void setLaunchThreads(int threads);
    int launchBatchSize() const;
    void setLaunchBatchSize(int size);
    int launchParallel() const;
    void setLaunchParallel(int parallel);
    QString launchFlashAttn() const;
    void setLaunchFlashAttn(const QString &value);
    QString launchCacheTypeK() const;
    void setLaunchCacheTypeK(const QString &type);
    QString launchCacheTypeV() const;
    void setLaunchCacheTypeV(const QString &type);
    bool launchNoMmap() const;
    void setLaunchNoMmap(bool on);
    bool launchJinja() const;
    void setLaunchJinja(bool on);
    QString launchExtraArgs() const;
    void setLaunchExtraArgs(const QString &args);

    // --- Hugging Face ---
    QString hfToken() const;
    void setHfToken(const QString &token);

public:
    // Connection mode constants (values stored in QSettings).
    static constexpr const char *kModeExternal = "external";
    static constexpr const char *kModeManaged = "managed";
    static constexpr int kCurrentSetupVersion = 1;

signals:
    void baseUrlChanged();
    void apiKeyChanged();
    void connectionTimeoutMsChanged();
    void modelNameChanged();
    void temperatureChanged();
    void maxTokensChanged();
    void dryMultiplierChanged();
    void dryBaseChanged();
    void dryAllowedLengthChanged();
    void dryPenaltyLastNChanged();
    void parserIdChanged();
    void themeModeChanged();
    void languageChanged();
    void windowXChanged();
    void windowYChanged();
    void windowWidthChanged();
    void windowHeightChanged();
    void windowStateChanged();
    void connectionModeChanged();
    void lastExternalBaseUrlChanged();
    void setupVersionChanged();
    void setupDismissedChanged();
    void serverPathChanged();
    void serverPathIsManagedChanged();
    void runtimeRootDirChanged();
    void runtimeModelsDirChanged();
    void runtimeBackendChanged();
    void installedBuildChanged();
    void autoStartChanged();
    void startOnDemandChanged();
    void stopOnExitChanged();
    void autoRestartChanged();
    void startupTimeoutMsChanged();
    void checkUpdatesChanged();
    void allowNonLoopbackChanged();
    void launchPresetIdChanged();
    void launchModelPathChanged();
    void launchMmprojPathChanged();
    void launchModelAliasChanged();
    void launchHostChanged();
    void launchPortChanged();
    void launchCtxSizeChanged();
    void launchGpuLayersChanged();
    void launchThreadsChanged();
    void launchBatchSizeChanged();
    void launchParallelChanged();
    void launchFlashAttnChanged();
    void launchCacheTypeKChanged();
    void launchCacheTypeVChanged();
    void launchNoMmapChanged();
    void launchJinjaChanged();
    void launchExtraArgsChanged();
    void hfTokenChanged();

private:
    QSettings m_settings;

    // Connection
    static constexpr const char *kBaseUrl = "provider/baseUrl";
    static constexpr const char *kApiKey = "provider/apiKey";
    static constexpr const char *kTimeoutMs = "provider/timeoutMs";

    // Model
    static constexpr const char *kModelName = "model/name";
    static constexpr const char *kTemperature = "model/temperature";
    static constexpr const char *kMaxTokens = "model/maxTokens";
    static constexpr const char *kDryMultiplier = "model/dryMultiplier";
    static constexpr const char *kDryBase = "model/dryBase";
    static constexpr const char *kDryAllowedLength = "model/dryAllowedLength";
    static constexpr const char *kDryPenaltyLastN = "model/dryPenaltyLastN";

    // Output / parser
    static constexpr const char *kParserId = "output/parser";

    // UI
    static constexpr const char *kThemeMode = "ui/theme";
    static constexpr const char *kLanguage = "ui/language";
    static constexpr const char *kWindowX = "ui/windowX";
    static constexpr const char *kWindowY = "ui/windowY";
    static constexpr const char *kWindowWidth = "ui/windowWidth";
    static constexpr const char *kWindowHeight = "ui/windowHeight";
    static constexpr const char *kWindowState = "ui/windowState";

    // Connection mode
    static constexpr const char *kConnectionMode = "provider/mode";
    static constexpr const char *kLastExternalBaseUrl = "provider/lastExternalBaseUrl";

    // Runtime (setup)
    static constexpr const char *kSetupVersion = "runtime/setupVersion";
    static constexpr const char *kSetupDismissed = "runtime/setupDismissed";
    static constexpr const char *kServerPath = "runtime/serverPath";
    static constexpr const char *kServerPathIsManaged = "runtime/serverPathIsManaged";
    static constexpr const char *kRuntimeRootDir = "runtime/rootDir";
    static constexpr const char *kRuntimeModelsDir = "runtime/modelsDir";
    static constexpr const char *kRuntimeBackend = "runtime/backend";
    static constexpr const char *kInstalledBuild = "runtime/installedBuild";
    static constexpr const char *kAutoStart = "runtime/autoStart";
    static constexpr const char *kStartOnDemand = "runtime/startOnDemand";
    static constexpr const char *kStopOnExit = "runtime/stopOnExit";
    static constexpr const char *kAutoRestart = "runtime/autoRestart";
    static constexpr const char *kStartupTimeoutMs = "runtime/startupTimeoutMs";
    static constexpr const char *kCheckUpdates = "runtime/checkUpdates";
    static constexpr const char *kAllowNonLoopback = "runtime/allowNonLoopback";

    // Launch
    static constexpr const char *kLaunchPresetId = "launch/presetId";
    static constexpr const char *kLaunchModelPath = "launch/modelPath";
    static constexpr const char *kLaunchMmprojPath = "launch/mmprojPath";
    static constexpr const char *kLaunchModelAlias = "launch/modelAlias";
    static constexpr const char *kLaunchHost = "launch/host";
    static constexpr const char *kLaunchPort = "launch/port";
    static constexpr const char *kLaunchCtxSize = "launch/ctxSize";
    static constexpr const char *kLaunchGpuLayers = "launch/gpuLayers";
    static constexpr const char *kLaunchThreads = "launch/threads";
    static constexpr const char *kLaunchBatchSize = "launch/batchSize";
    static constexpr const char *kLaunchParallel = "launch/parallel";
    static constexpr const char *kLaunchFlashAttn = "launch/flashAttn";
    static constexpr const char *kLaunchCacheTypeK = "launch/cacheTypeK";
    static constexpr const char *kLaunchCacheTypeV = "launch/cacheTypeV";
    static constexpr const char *kLaunchNoMmap = "launch/noMmap";
    static constexpr const char *kLaunchJinja = "launch/jinja";
    static constexpr const char *kLaunchExtraArgs = "launch/extraArgs";

    // Hugging Face
    static constexpr const char *kHfToken = "hf/token";
};

}  // namespace llocr
