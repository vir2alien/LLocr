#pragma once

#include <functional>

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>

#include "runtime/ConnectionMode.h"

namespace llocr {

class SettingsStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(int connectionTimeoutMs READ connectionTimeoutMs WRITE setConnectionTimeoutMs NOTIFY connectionTimeoutMsChanged)
    Q_PROPERTY(QString modelName READ modelName WRITE setModelName NOTIFY modelNameChanged)
    Q_PROPERTY(QString modelRecipeId READ modelRecipeId WRITE setModelRecipeId NOTIFY modelRecipeIdChanged)
    Q_PROPERTY(QString parserId READ parserId WRITE setParserId NOTIFY parserIdChanged)
    Q_PROPERTY(bool splitPages READ splitPages WRITE setSplitPages NOTIFY splitPagesChanged)
    Q_PROPERTY(bool keepPageNumbers READ keepPageNumbers WRITE setKeepPageNumbers NOTIFY keepPageNumbersChanged)
    Q_PROPERTY(bool pdfLandscape READ pdfLandscape WRITE setPdfLandscape NOTIFY pdfLandscapeChanged)
    Q_PROPERTY(int pdfMarginMm READ pdfMarginMm WRITE setPdfMarginMm NOTIFY pdfMarginMmChanged)
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int windowX READ windowX WRITE setWindowX NOTIFY windowXChanged)
    Q_PROPERTY(int windowY READ windowY WRITE setWindowY NOTIFY windowYChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY windowWidthChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowHeightChanged)
    Q_PROPERTY(int windowState READ windowState WRITE setWindowState NOTIFY windowStateChanged)

    Q_PROPERTY(QString connectionMode READ connectionMode WRITE setConnectionMode NOTIFY connectionModeChanged)
    Q_PROPERTY(QString lastExternalBaseUrl READ lastExternalBaseUrl WRITE setLastExternalBaseUrl NOTIFY lastExternalBaseUrlChanged)

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
    Q_PROPERTY(bool allowNonLoopback READ allowNonLoopback WRITE setAllowNonLoopback NOTIFY allowNonLoopbackChanged)

    Q_PROPERTY(QString launchPresetId READ launchPresetId WRITE setLaunchPresetId NOTIFY launchPresetIdChanged)
    Q_PROPERTY(QString launchProfileId READ launchProfileId WRITE setLaunchProfileId NOTIFY launchProfileIdChanged)
    Q_PROPERTY(QString launchModelPath READ launchModelPath WRITE setLaunchModelPath NOTIFY launchModelPathChanged)
    Q_PROPERTY(QString launchMmprojPath READ launchMmprojPath WRITE setLaunchMmprojPath NOTIFY launchMmprojPathChanged)
    Q_PROPERTY(QString launchModelAlias READ launchModelAlias WRITE setLaunchModelAlias NOTIFY launchModelAliasChanged)
    Q_PROPERTY(QString launchHost READ launchHost WRITE setLaunchHost NOTIFY launchHostChanged)
    Q_PROPERTY(int launchPort READ launchPort WRITE setLaunchPort NOTIFY launchPortChanged)

    // Verification (check) model: its own model/mmproj locations, request and
    // launch profile ids, and the model name used against an external server
    // (which may host several models). The single managed server instance is
    // (re)launched per task with the model the task needs.
    Q_PROPERTY(QString checkLaunchModelPath READ checkLaunchModelPath WRITE setCheckLaunchModelPath NOTIFY checkLaunchModelPathChanged)
    Q_PROPERTY(QString checkLaunchMmprojPath READ checkLaunchMmprojPath WRITE setCheckLaunchMmprojPath NOTIFY checkLaunchMmprojPathChanged)
    Q_PROPERTY(QString checkRequestProfileId READ checkRequestProfileId WRITE setCheckRequestProfileId NOTIFY checkRequestProfileIdChanged)
    Q_PROPERTY(QString checkLaunchProfileId READ checkLaunchProfileId WRITE setCheckLaunchProfileId NOTIFY checkLaunchProfileIdChanged)
    Q_PROPERTY(QString checkModelName READ checkModelName WRITE setCheckModelName NOTIFY checkModelNameChanged)

    Q_PROPERTY(QString hfToken READ hfToken WRITE setHfToken NOTIFY hfTokenChanged)

public:
    explicit SettingsStore(QObject *parent = nullptr);

    Q_INVOKABLE void forceSave();
    Q_INVOKABLE void resetToDefaults();
    // Per-window scoped resets (ADR 75): they write the matching rows of the
    // kDefaults table through the property setters (NOTIFY preserved).
    Q_INVOKABLE void resetOutputDefaults();
    Q_INVOKABLE void resetRuntimeDefaults();
    Q_INVOKABLE bool contains(const QString &key) const;

    struct SettingDefault
    {
        const char *key;
        const char *property;
        QVariant defaultValue;
    };
    static const SettingDefault *defaults();
    static int defaultsCount();

    void applyStartupMigration();

    QString baseUrl() const;
    void setBaseUrl(const QString &url);
    QString apiKey() const;
    void setApiKey(const QString &key);
    int connectionTimeoutMs() const;
    void setConnectionTimeoutMs(int timeOut);

    QString modelName() const;
    void setModelName(const QString &modelName);

    QString modelRecipeId() const;
    void setModelRecipeId(const QString &recipeId);

    QString parserId() const;
    void setParserId(const QString &parserName);

    bool splitPages() const;
    void setSplitPages(bool on);
    bool keepPageNumbers() const;
    void setKeepPageNumbers(bool on);
    bool pdfLandscape() const;
    void setPdfLandscape(bool on);
    int pdfMarginMm() const;
    void setPdfMarginMm(int mm);

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

    QString connectionMode() const;
    void setConnectionMode(const QString &mode);
    ConnectionMode mode() const;
    void setMode(ConnectionMode mode);
    QString lastExternalBaseUrl() const;
    void setLastExternalBaseUrl(const QString &url);

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
    bool allowNonLoopback() const;
    void setAllowNonLoopback(bool on);

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
    QString launchProfileId() const;
    void setLaunchProfileId(const QString &id);

    QString checkLaunchModelPath() const;
    void setCheckLaunchModelPath(const QString &path);
    QString checkLaunchMmprojPath() const;
    void setCheckLaunchMmprojPath(const QString &path);
    QString checkRequestProfileId() const;
    void setCheckRequestProfileId(const QString &id);
    QString checkLaunchProfileId() const;
    void setCheckLaunchProfileId(const QString &id);
    QString checkModelName() const;
    void setCheckModelName(const QString &name);

    QString hfToken() const;
    void setHfToken(const QString &token);

signals:
    void baseUrlChanged();
    void apiKeyChanged();
    void connectionTimeoutMsChanged();
    void modelNameChanged();
    void modelRecipeIdChanged();
    void parserIdChanged();
    void splitPagesChanged();
    void keepPageNumbersChanged();
    void pdfLandscapeChanged();
    void pdfMarginMmChanged();
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
    void allowNonLoopbackChanged();
    void launchPresetIdChanged();
    void launchProfileIdChanged();
    void launchModelPathChanged();
    void launchMmprojPathChanged();
    void launchModelAliasChanged();
    void launchHostChanged();
    void launchPortChanged();
    void checkLaunchModelPathChanged();
    void checkLaunchMmprojPathChanged();
    void checkRequestProfileIdChanged();
    void checkLaunchProfileIdChanged();
    void checkModelNameChanged();
    void hfTokenChanged();

public:
    static constexpr const char *kModeExternal = "external";
    static constexpr const char *kModeManaged = "managed";
    static constexpr int kCurrentSetupVersion = 1;
    static constexpr const char *kDefaultBaseUrl = "http://localhost:8080";
    static constexpr const char *kDefaultApiKey = "";
    static constexpr int kDefaultTimeoutMs = 120000;
    static constexpr const char *kDefaultModelName = "Unlimited-OCR";
    static constexpr const char *kDefaultModelRecipeId = "unlimited-ocr";
    static constexpr const char *kDefaultParserId = "det_tokens";
    static constexpr bool kDefaultSplitPages = true;
    static constexpr bool kDefaultKeepPageNumbers = true;
    static constexpr bool kDefaultPdfLandscape = false;
    static constexpr int kDefaultPdfMarginMm = 15;
    static constexpr int kMaxPdfMarginMm = 50;
    static constexpr int kDefaultThemeMode = 0;  // System
    static constexpr const char *kDefaultLanguage = "system";
    static constexpr const char *kDefaultModelAlias = "llocr-local";
    static constexpr const char *kDefaultHost = "127.0.0.1";
    static constexpr int kDefaultPort = 0;  // 0 = auto-pick
    static constexpr int kDefaultStartupTimeoutMs = 180000;

private:
    static QSettings makeSettings();
    QSettings m_settings = makeSettings();
    static const SettingDefault kDefaults[];

    // Writes the default value for every kDefaults row whose key matches.
    void resetGroup(const std::function<bool(const QString &)> &matches);

    static constexpr const char *kBaseUrl = "provider/baseUrl";
    static constexpr const char *kApiKey = "provider/apiKey";
    static constexpr const char *kTimeoutMs = "provider/timeoutMs";

    static constexpr const char *kModelName = "model/name";
    static constexpr const char *kModelRecipeId = "model/recipeId";
    static constexpr const char *kParserId = "parser/id";

    // Output / export
    static constexpr const char *kSplitPages = "output/splitPages";
    static constexpr const char *kKeepPageNumbers = "output/keepPageNumbers";
    static constexpr const char *kPdfLandscape = "export/pdfLandscape";
    static constexpr const char *kPdfMarginMm = "export/pdfMarginMm";

    static constexpr const char *kThemeMode = "ui/theme";
    static constexpr const char *kLanguage = "ui/language";
    static constexpr const char *kWindowX = "ui/windowX";
    static constexpr const char *kWindowY = "ui/windowY";
    static constexpr const char *kWindowWidth = "ui/windowWidth";
    static constexpr const char *kWindowHeight = "ui/windowHeight";
    static constexpr const char *kWindowState = "ui/windowState";

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
    static constexpr const char *kAllowNonLoopback = "runtime/allowNonLoopback";

    // Launch
    static constexpr const char *kLaunchPresetId = "launch/presetId";
    static constexpr const char *kLaunchProfileId = "launch/profileId";
    static constexpr const char *kLaunchModelPath = "launch/modelPath";
    static constexpr const char *kLaunchMmprojPath = "launch/mmprojPath";
    static constexpr const char *kLaunchModelAlias = "launch/modelAlias";
    static constexpr const char *kLaunchHost = "launch/host";
    static constexpr const char *kLaunchPort = "launch/port";

    // Verification (check) model
    static constexpr const char *kCheckLaunchModelPath = "check/modelPath";
    static constexpr const char *kCheckLaunchMmprojPath = "check/mmprojPath";
    static constexpr const char *kCheckRequestProfileId = "check/requestProfileId";
    static constexpr const char *kCheckLaunchProfileId = "check/launchProfileId";
    static constexpr const char *kCheckModelName = "check/modelName";

    // Hugging Face
    static constexpr const char *kHfToken = "hf/token";
};

}  // namespace llocr
