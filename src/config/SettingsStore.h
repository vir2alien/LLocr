#pragma once

#include <functional>

#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "runtime/ConnectionMode.h"

namespace llocr {

class SettingsStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
    // The API key is a secret: it lives in the OS secret store and is never
    // handed to the QML — the UI sees only whether a key is set (ADR 227).
    Q_PROPERTY(bool apiKeySet READ apiKeySet NOTIFY secretsChanged)
    Q_PROPERTY(bool secretStoreAvailable READ secretStoreAvailable CONSTANT)
    Q_PROPERTY(QString secretStoreName READ secretStoreName CONSTANT)
    // The reason the last read or write of a secret failed. Empty when the
    // store answered. The UI shows it: a key that is not there must say why,
    // and "the key is gone" is not a reason (ADR 227).
    Q_PROPERTY(QString secretError READ secretError NOTIFY secretsChanged)
    // False means the OS store refused the last key: it lives in the memory
    // until the app exits, and the UI must say so.
    Q_PROPERTY(bool lastKeyPersisted READ lastKeyPersisted NOTIFY secretsChanged)
    Q_PROPERTY(int connectionTimeoutMs READ connectionTimeoutMs WRITE setConnectionTimeoutMs NOTIFY connectionTimeoutMsChanged)
    Q_PROPERTY(int responseTimeoutMs READ responseTimeoutMs WRITE setResponseTimeoutMs NOTIFY responseTimeoutMsChanged)
    Q_PROPERTY(QString modelName READ modelName WRITE setModelName NOTIFY modelNameChanged)
    Q_PROPERTY(QString modelRecipeId READ modelRecipeId WRITE setModelRecipeId NOTIFY modelRecipeIdChanged)
    Q_PROPERTY(QString parserId READ parserId WRITE setParserId NOTIFY parserIdChanged)
    Q_PROPERTY(bool orientationCheckEnabled READ orientationCheckEnabled WRITE setOrientationCheckEnabled NOTIFY orientationCheckEnabledChanged)
    Q_PROPERTY(bool orientationAutoTurnEnabled READ orientationAutoTurnEnabled WRITE setOrientationAutoTurnEnabled NOTIFY orientationAutoTurnEnabledChanged)
    Q_PROPERTY(bool splitPages READ splitPages WRITE setSplitPages NOTIFY splitPagesChanged)
    Q_PROPERTY(bool keepPageNumbers READ keepPageNumbers WRITE setKeepPageNumbers NOTIFY keepPageNumbersChanged)
    Q_PROPERTY(bool tablesAsHtml READ tablesAsHtml WRITE setTablesAsHtml NOTIFY tablesAsHtmlChanged)
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

    Q_PROPERTY(QString launchProfileId READ launchProfileId WRITE setLaunchProfileId NOTIFY launchProfileIdChanged)
    Q_PROPERTY(QString launchModelPath READ launchModelPath WRITE setLaunchModelPath NOTIFY launchModelPathChanged)
    Q_PROPERTY(QString launchMmprojPath READ launchMmprojPath WRITE setLaunchMmprojPath NOTIFY launchMmprojPathChanged)
    Q_PROPERTY(QString launchDraftPath READ launchDraftPath WRITE setLaunchDraftPath NOTIFY launchDraftPathChanged)
    Q_PROPERTY(QString launchHost READ launchHost WRITE setLaunchHost NOTIFY launchHostChanged)
    Q_PROPERTY(int launchPort READ launchPort WRITE setLaunchPort NOTIFY launchPortChanged)

    Q_PROPERTY(QString checkLaunchModelPath READ checkLaunchModelPath WRITE setCheckLaunchModelPath NOTIFY checkLaunchModelPathChanged)
    Q_PROPERTY(QString checkLaunchMmprojPath READ checkLaunchMmprojPath WRITE setCheckLaunchMmprojPath NOTIFY checkLaunchMmprojPathChanged)
    Q_PROPERTY(QString checkLaunchDraftPath READ checkLaunchDraftPath WRITE setCheckLaunchDraftPath NOTIFY checkLaunchDraftPathChanged)
    Q_PROPERTY(QString checkRequestProfileId READ checkRequestProfileId WRITE setCheckRequestProfileId NOTIFY checkRequestProfileIdChanged)
    Q_PROPERTY(QString checkModelName READ checkModelName WRITE setCheckModelName NOTIFY checkModelNameChanged)
    Q_PROPERTY(bool showConfidence READ showConfidence WRITE setShowConfidence NOTIFY showConfidenceChanged)

    Q_PROPERTY(QString decisionLaunchModelPath READ decisionLaunchModelPath WRITE setDecisionLaunchModelPath NOTIFY decisionLaunchModelPathChanged)
    Q_PROPERTY(QString decisionLaunchMmprojPath READ decisionLaunchMmprojPath WRITE setDecisionLaunchMmprojPath NOTIFY decisionLaunchMmprojPathChanged)
    Q_PROPERTY(QString decisionLaunchDraftPath READ decisionLaunchDraftPath WRITE setDecisionLaunchDraftPath NOTIFY decisionLaunchDraftPathChanged)
    Q_PROPERTY(QString decisionRequestProfileId READ decisionRequestProfileId WRITE setDecisionRequestProfileId NOTIFY decisionRequestProfileIdChanged)
    Q_PROPERTY(QString decisionModelName READ decisionModelName WRITE setDecisionModelName NOTIFY decisionModelNameChanged)
    Q_PROPERTY(double decisionMatchThreshold READ decisionMatchThreshold WRITE setDecisionMatchThreshold NOTIFY decisionMatchThresholdChanged)
    Q_PROPERTY(double defaultDecisionMatchThreshold READ defaultDecisionMatchThreshold CONSTANT)

    Q_PROPERTY(QString layoutLaunchModelPath READ layoutLaunchModelPath WRITE setLayoutLaunchModelPath NOTIFY layoutLaunchModelPathChanged)
    Q_PROPERTY(QString layoutLaunchMmprojPath READ layoutLaunchMmprojPath WRITE setLayoutLaunchMmprojPath NOTIFY layoutLaunchMmprojPathChanged)
    Q_PROPERTY(QString layoutLaunchDraftPath READ layoutLaunchDraftPath WRITE setLayoutLaunchDraftPath NOTIFY layoutLaunchDraftPathChanged)
    Q_PROPERTY(QString layoutRequestProfileId READ layoutRequestProfileId WRITE setLayoutRequestProfileId NOTIFY layoutRequestProfileIdChanged)
    Q_PROPERTY(QString layoutModelName READ layoutModelName WRITE setLayoutModelName NOTIFY layoutModelNameChanged)

    Q_PROPERTY(QString rawPageOcrLaunchModelPath READ rawPageOcrLaunchModelPath WRITE setRawPageOcrLaunchModelPath NOTIFY rawPageOcrLaunchModelPathChanged)
    Q_PROPERTY(QString rawPageOcrLaunchMmprojPath READ rawPageOcrLaunchMmprojPath WRITE setRawPageOcrLaunchMmprojPath NOTIFY rawPageOcrLaunchMmprojPathChanged)
    Q_PROPERTY(QString rawPageOcrLaunchDraftPath READ rawPageOcrLaunchDraftPath WRITE setRawPageOcrLaunchDraftPath NOTIFY rawPageOcrLaunchDraftPathChanged)
    Q_PROPERTY(QString rawPageOcrRequestProfileId READ rawPageOcrRequestProfileId WRITE setRawPageOcrRequestProfileId NOTIFY rawPageOcrRequestProfileIdChanged)
    Q_PROPERTY(QString rawPageOcrModelName READ rawPageOcrModelName WRITE setRawPageOcrModelName NOTIFY rawPageOcrModelNameChanged)

    Q_PROPERTY(QString fixLaunchModelPath READ fixLaunchModelPath WRITE setFixLaunchModelPath NOTIFY fixLaunchModelPathChanged)
    Q_PROPERTY(QString fixLaunchMmprojPath READ fixLaunchMmprojPath WRITE setFixLaunchMmprojPath NOTIFY fixLaunchMmprojPathChanged)
    Q_PROPERTY(QString fixLaunchDraftPath READ fixLaunchDraftPath WRITE setFixLaunchDraftPath NOTIFY fixLaunchDraftPathChanged)
    Q_PROPERTY(QString fixRequestProfileId READ fixRequestProfileId WRITE setFixRequestProfileId NOTIFY fixRequestProfileIdChanged)
    Q_PROPERTY(QString fixModelName READ fixModelName WRITE setFixModelName NOTIFY fixModelNameChanged)

    Q_PROPERTY(QString hfToken READ hfToken WRITE setHfToken NOTIFY hfTokenChanged)

public:
    explicit SettingsStore(QObject *parent = nullptr);

    // The namespace of the secret ids. The tests point it at their own, so a
    // test never writes into (or deletes from) the app's own keychain
    // entries.
    static void setSecretNamespace(const QString &prefix);
    static QString secretNamespace();

    Q_INVOKABLE void forceSave();
    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE void resetOutputDefaults();
    Q_INVOKABLE void resetRuntimeDefaults();
    Q_INVOKABLE bool contains(const QString &key) const;

    struct SettingDefault {
        const char *key;
        const char *property;
        QVariant defaultValue;
    };
    static const SettingDefault *defaults();
    static int defaultsCount();

    QString baseUrl() const;
    void setBaseUrl(const QString &url);

    // The shared API key of the external connection. The getter serves the
    // request path; the QML writes the key and reads only its presence.
    QString apiKey() const;
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void clearApiKey();
    bool apiKeySet() const;

    // Asks the store for the key again and tells the UI what it answered.
    // The read is what finds out that the item belongs to another build of
    // the app, and the UI has to show that reason (ADR 233).
    Q_INVOKABLE void refreshSecretState();

    // The external connection of one task (ADR 226): its own endpoint, its
    // own key, its own alias. An empty endpoint means the shared one.
    Q_INVOKABLE QString baseUrlForRole(const QString &role) const;
    Q_INVOKABLE void setBaseUrlForRole(const QString &role, const QString &url);
    Q_INVOKABLE QString effectiveBaseUrlForRole(const QString &role) const;
    Q_INVOKABLE bool apiKeySetForRole(const QString &role) const;
    Q_INVOKABLE void setApiKeyForRole(const QString &role, const QString &key);
    Q_INVOKABLE void clearApiKeyForRole(const QString &role);
    QString apiKeyForRole(const QString &role) const;
    // The ids of the secrets a task may use: its own key and the shared one.
    QStringList secretIdsForRole(const QString &role) const;

    bool secretStoreAvailable() const;
    QString secretStoreName() const;
    QString secretError() const;
    bool lastKeyPersisted() const;

    int connectionTimeoutMs() const;
    void setConnectionTimeoutMs(int ms);
    int responseTimeoutMs() const;
    void setResponseTimeoutMs(int ms);

    QString modelName() const;
    void setModelName(const QString &modelName);

    QString modelRecipeId() const;
    void setModelRecipeId(const QString &recipeId);

    void selectModelProfile(const QString &repo, const QString &role);

    QString parserId() const;
    void setParserId(const QString &parserName);

    // The orientation check that runs before a page is recognized: a page
    // that lies the wrong way up is turned first.
    bool orientationCheckEnabled() const;
    bool orientationAutoTurnEnabled() const;
    void setOrientationAutoTurnEnabled(bool on);
    void setOrientationCheckEnabled(bool on);

    bool splitPages() const;
    void setSplitPages(bool on);
    bool keepPageNumbers() const;
    void setKeepPageNumbers(bool on);
    bool tablesAsHtml() const;
    void setTablesAsHtml(bool on);
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

    QString launchModelPath() const;
    void setLaunchModelPath(const QString &path);
    QString launchMmprojPath() const;
    void setLaunchMmprojPath(const QString &path);
    QString launchDraftPath() const;
    void setLaunchDraftPath(const QString &path);
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
    QString checkLaunchDraftPath() const;
    void setCheckLaunchDraftPath(const QString &path);
    QString checkRequestProfileId() const;
    void setCheckRequestProfileId(const QString &id);
    QString checkModelName() const;
    void setCheckModelName(const QString &name);
    bool showConfidence() const;
    void setShowConfidence(bool on);

    QString decisionLaunchModelPath() const;
    void setDecisionLaunchModelPath(const QString &path);
    QString decisionLaunchMmprojPath() const;
    void setDecisionLaunchMmprojPath(const QString &path);
    QString decisionLaunchDraftPath() const;
    void setDecisionLaunchDraftPath(const QString &path);
    QString decisionRequestProfileId() const;
    void setDecisionRequestProfileId(const QString &id);
    QString decisionModelName() const;
    void setDecisionModelName(const QString &name);
    double decisionMatchThreshold() const;
    double defaultDecisionMatchThreshold() const;
    void setDecisionMatchThreshold(double threshold);

    QString layoutLaunchModelPath() const;
    void setLayoutLaunchModelPath(const QString &path);
    QString layoutLaunchMmprojPath() const;
    void setLayoutLaunchMmprojPath(const QString &path);
    QString layoutLaunchDraftPath() const;
    void setLayoutLaunchDraftPath(const QString &path);
    QString layoutRequestProfileId() const;
    void setLayoutRequestProfileId(const QString &id);
    QString layoutModelName() const;
    void setLayoutModelName(const QString &name);

    QString rawPageOcrLaunchModelPath() const;
    void setRawPageOcrLaunchModelPath(const QString &path);
    QString rawPageOcrLaunchMmprojPath() const;
    void setRawPageOcrLaunchMmprojPath(const QString &path);
    QString rawPageOcrLaunchDraftPath() const;
    void setRawPageOcrLaunchDraftPath(const QString &path);
    QString rawPageOcrRequestProfileId() const;
    void setRawPageOcrRequestProfileId(const QString &id);
    QString rawPageOcrModelName() const;
    void setRawPageOcrModelName(const QString &name);

    QString fixLaunchModelPath() const;
    void setFixLaunchModelPath(const QString &path);
    QString fixLaunchMmprojPath() const;
    void setFixLaunchMmprojPath(const QString &path);
    QString fixLaunchDraftPath() const;
    void setFixLaunchDraftPath(const QString &path);
    QString fixRequestProfileId() const;
    void setFixRequestProfileId(const QString &id);
    QString fixModelName() const;
    void setFixModelName(const QString &name);

    // The ids of the app's tasks, in the order the task table shows them. The
    // list comes from ConnectionRole, the single source of the roles
    // (ADR 215) — the QML does not keep its own copy.
    Q_PROPERTY(QStringList taskRoles READ taskRoles CONSTANT)

    // Task-generic views over the per-task model settings. role is a task id:
    // "pageOcr", "rawPageOcr", "blockRecognition", "decision", "layout" or
    // "fix" (the legacy "ocr" maps to "pageOcr").
    Q_INVOKABLE QString launchModelPathForRole(const QString &role) const;
    void setLaunchModelPathForRole(const QString &role, const QString &path);
    QString launchMmprojPathForRole(const QString &role) const;
    void setLaunchMmprojPathForRole(const QString &role, const QString &path);
    QString launchDraftPathForRole(const QString &role) const;
    void setLaunchDraftPathForRole(const QString &role, const QString &path);
    QString requestProfileIdForRole(const QString &role) const;
    Q_INVOKABLE QString modelNameForRole(const QString &role) const;
    Q_INVOKABLE void setModelNameForRole(const QString &role, const QString &name);
    QStringList taskRoles() const;

    QString hfToken() const;
    void setHfToken(const QString &token);

    QString selectedQuant(const QString &profileId) const;
    void setSelectedQuant(const QString &profileId, const QString &quantId);

signals:
    void baseUrlChanged();
    void secretsChanged();
    void connectionTimeoutMsChanged();
    void responseTimeoutMsChanged();
    void modelNameChanged();
    void modelRecipeIdChanged();
    void parserIdChanged();
    void orientationCheckEnabledChanged();
    void orientationAutoTurnEnabledChanged();
    void splitPagesChanged();
    void keepPageNumbersChanged();
    void tablesAsHtmlChanged();
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
    void launchProfileIdChanged();
    void launchModelPathChanged();
    void launchMmprojPathChanged();
    void launchDraftPathChanged();
    void launchHostChanged();
    void launchPortChanged();
    void checkLaunchModelPathChanged();
    void checkLaunchMmprojPathChanged();
    void checkLaunchDraftPathChanged();
    void checkRequestProfileIdChanged();
    void checkModelNameChanged();
    void showConfidenceChanged();
    void decisionLaunchModelPathChanged();
    void decisionLaunchMmprojPathChanged();
    void decisionLaunchDraftPathChanged();
    void decisionRequestProfileIdChanged();
    void decisionModelNameChanged();
    void decisionMatchThresholdChanged();
    void layoutLaunchModelPathChanged();
    void layoutLaunchMmprojPathChanged();
    void layoutLaunchDraftPathChanged();
    void layoutRequestProfileIdChanged();
    void layoutModelNameChanged();
    void rawPageOcrLaunchModelPathChanged();
    void rawPageOcrLaunchMmprojPathChanged();
    void rawPageOcrLaunchDraftPathChanged();
    void rawPageOcrRequestProfileIdChanged();
    void rawPageOcrModelNameChanged();
    void fixLaunchModelPathChanged();
    void fixLaunchMmprojPathChanged();
    void fixLaunchDraftPathChanged();
    void fixRequestProfileIdChanged();
    void fixModelNameChanged();
    void hfTokenChanged();

public:
    static constexpr const char *kModeExternal = "external";
    static constexpr const char *kModeManaged = "managed";
    static constexpr int kCurrentSetupVersion = 1;
    // The settings file's schema. A file without the current version is wiped
    // once on startup — the reset keeps the runtime paths and the code's
    // defaults answer for everything else.
    static constexpr const char *kSettingsSchemaVersion = "settings/schemaVersion";
    static constexpr int kCurrentSettingsSchemaVersion = 1;
    static constexpr const char *kDefaultBaseUrl = "http://localhost:8080";
    static constexpr int kDefaultConnectionTimeoutMs = 5000;
    static constexpr int kDefaultResponseTimeoutMs = 300000;
    static constexpr const char *kDefaultModelName = "MinerU2.5-Pro-2605-1.2B";
    static constexpr const char *kDefaultModelRecipeId = "mineru";
    static constexpr const char *kDefaultParserId = "auto";
    static constexpr bool kDefaultSplitPages = true;
    static constexpr bool kDefaultKeepPageNumbers = true;
    static constexpr bool kDefaultTablesAsHtml = false;
    static constexpr bool kDefaultPdfLandscape = false;
    static constexpr int kDefaultPdfMarginMm = 15;
    static constexpr int kMaxPdfMarginMm = 50;
    static constexpr int kDefaultThemeMode = 0;  // System
    static constexpr const char *kDefaultLanguage = "system";
    static constexpr const char *kDefaultHost = "127.0.0.1";
    static constexpr int kDefaultPort = 0;  // 0 = auto-pick
    static constexpr int kDefaultStartupTimeoutMs = 180000;
    static constexpr int kMinTimeoutMs = 1000;
    static constexpr int kMaxConnectionTimeoutMs = 600000;
    static constexpr int kMaxResponseTimeoutMs = 3600000;
    static constexpr int kMaxStartupTimeoutMs = 3600000;
    static constexpr int kMaxPort = 65535;
    static constexpr double kDefaultDecisionMatchThreshold = 0.5;

private:
    static QSettings makeSettings();
    QSettings m_settings = makeSettings();
    static const QList<SettingDefault> &defaultTable();

    void resetGroup(const std::function<bool(const QString &)> &matches);

    // The secrets live in the OS store (ADR 227); these helpers name them and
    // move the values in and out of it.
    QString secretId(const QString &id) const;
    QString readSecret(const QString &id) const;
    bool persistSecret(const QString &id, const QString &value);
    void dropSecret(const QString &id);
    void clearSecrets();
    void clearTaskConnections();
    // The group of the settings a task's connection row reads and writes. The
    // block recognition task keeps the check name its accessors carry; the
    // other tasks are their own group.
    static QString taskKeyName(const QString &role);
    static QString taskBaseUrlKey(const QString &role);
    static QString taskSecretId(const QString &role);

    bool m_lastKeyPersisted = true;

    static constexpr const char *kBaseUrl = "provider/baseUrl";
    static constexpr const char *kApiKey = "provider/apiKey";
    static constexpr const char *kConnectionTimeoutKey = "provider/connectionTimeoutMs";
    static constexpr const char *kResponseTimeoutKey = "provider/responseTimeoutMs";

    static constexpr const char *kModelName = "model/name";
    static constexpr const char *kModelRecipeId = "model/recipeId";
    static constexpr const char *kParserId = "parser/id";

    // The orientation check before the recognition
    static constexpr const char *kOrientationCheck = "recognition/orientationCheck";
    static constexpr const char *kDefaultOrientationCheck = "true";
    // The markup pass turns a sideways page itself when the model's rotation
    // tokens vote one way (ADR 246); off leaves the one-click suggestion.
    static constexpr const char *kOrientationAutoTurn = "recognition/orientationAutoTurn";
    static constexpr const char *kDefaultOrientationAutoTurn = "true";

    // Output / export
    static constexpr const char *kSplitPages = "output/splitPages";
    static constexpr const char *kKeepPageNumbers = "output/keepPageNumbers";
    static constexpr const char *kTablesAsHtml = "output/tablesAsHtml";
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

    // Launch (shared server launch profile + host/port; the per-task model
    // files live in the tasks/* group below)
    static constexpr const char *kLaunchProfileId = "launch/profileId";
    static constexpr const char *kLaunchHost = "launch/host";
    static constexpr const char *kLaunchPort = "launch/port";

    // The page task's launch paths — the task is gone and its accessors go
    // with it; the check ones own the OCR model's files now.
    static constexpr const char *kLaunchModelPath = "tasks/pageOcr/modelPath";
    static constexpr const char *kLaunchMmprojPath = "tasks/pageOcr/mmprojPath";
    static constexpr const char *kLaunchDraftPath = "tasks/pageOcr/draftPath";

    // Block recognition: the OCR model's launch files and the block request's
    // request profile, in the check namespace its accessors were born with.
    static constexpr const char *kCheckLaunchModelPath = "tasks/check/modelPath";
    static constexpr const char *kCheckLaunchMmprojPath = "tasks/check/mmprojPath";
    static constexpr const char *kCheckLaunchDraftPath = "tasks/check/draftPath";
    static constexpr const char *kCheckRequestProfileId = "tasks/check/requestProfileId";
    static constexpr const char *kCheckModelName = "tasks/check/modelName";

    static constexpr const char *kShowConfidence = "check/showConfidence";

    // Decision model keys
    static constexpr const char *kDecisionLaunchModelPath = "tasks/decision/modelPath";
    static constexpr const char *kDecisionLaunchMmprojPath = "tasks/decision/mmprojPath";
    static constexpr const char *kDecisionLaunchDraftPath = "tasks/decision/draftPath";
    static constexpr const char *kDecisionRequestProfileId = "tasks/decision/requestProfileId";
    static constexpr const char *kDecisionModelName = "tasks/decision/modelName";
    static constexpr const char *kDecisionMatchThreshold = "decision/matchThreshold";

    // Layout (page markup) model keys
    static constexpr const char *kLayoutLaunchModelPath = "tasks/layout/modelPath";
    static constexpr const char *kLayoutLaunchMmprojPath = "tasks/layout/mmprojPath";
    static constexpr const char *kLayoutLaunchDraftPath = "tasks/layout/draftPath";
    static constexpr const char *kLayoutRequestProfileId = "tasks/layout/requestProfileId";
    static constexpr const char *kLayoutModelName = "tasks/layout/modelName";

    // Raw page OCR (page recognition without markup)
    static constexpr const char *kRawPageOcrLaunchModelPath = "tasks/rawPageOcr/modelPath";
    static constexpr const char *kRawPageOcrLaunchMmprojPath = "tasks/rawPageOcr/mmprojPath";
    static constexpr const char *kRawPageOcrLaunchDraftPath = "tasks/rawPageOcr/draftPath";
    static constexpr const char *kRawPageOcrRequestProfileId = "tasks/rawPageOcr/requestProfileId";
    static constexpr const char *kRawPageOcrModelName = "tasks/rawPageOcr/modelName";

    // Fix (block correction by a universal model)
    static constexpr const char *kFixLaunchModelPath = "tasks/fix/modelPath";
    static constexpr const char *kFixLaunchMmprojPath = "tasks/fix/mmprojPath";
    static constexpr const char *kFixLaunchDraftPath = "tasks/fix/draftPath";
    static constexpr const char *kFixRequestProfileId = "tasks/fix/requestProfileId";
    static constexpr const char *kFixModelName = "tasks/fix/modelName";

    // Hugging Face
    static constexpr const char *kHfToken = "hf/token";

    static constexpr const char *kQuantSelectionGroup = "models/quant";
};

}  // namespace llocr
