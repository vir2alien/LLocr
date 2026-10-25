#include "config/SettingsStore.h"

#include "core/ConnectionRole.h"
#include "core/ModelProfiles.h"

#include <algorithm>
#include <QCoreApplication>
#include <QMetaProperty>

namespace llocr {

namespace {

// The per-role settings: one record per role, so a new role is one row here
// instead of ten switches (ADR 216). The per-role getters and setters stay —
// they own their settings keys and their change signals.
struct RoleSettings {
    struct Path {
        QString (SettingsStore::*read)() const;
        void (SettingsStore::*write)(const QString &);
    };
    Path modelPath;
    Path mmprojPath;
    Path draftPath;
    QString (SettingsStore::*requestProfileId)() const;
    void (SettingsStore::*setRequestProfileId)(const QString &);
    QString (SettingsStore::*modelName)() const;
    void (SettingsStore::*setModelName)(const QString &);
};

const RoleSettings &roleSettings(ConnectionRole role)
{
    static const RoleSettings table[] = {
        // PageOcr: the page settings are the ones without a role prefix.
        {{&SettingsStore::launchModelPath, &SettingsStore::setLaunchModelPath},
         {&SettingsStore::launchMmprojPath, &SettingsStore::setLaunchMmprojPath},
         {&SettingsStore::launchDraftPath, &SettingsStore::setLaunchDraftPath},
         &SettingsStore::modelRecipeId,
         &SettingsStore::setModelRecipeId,
         &SettingsStore::modelName,
         &SettingsStore::setModelName},
        {{&SettingsStore::rawPageOcrLaunchModelPath, &SettingsStore::setRawPageOcrLaunchModelPath},
         {&SettingsStore::rawPageOcrLaunchMmprojPath, &SettingsStore::setRawPageOcrLaunchMmprojPath},
         {&SettingsStore::rawPageOcrLaunchDraftPath, &SettingsStore::setRawPageOcrLaunchDraftPath},
         &SettingsStore::rawPageOcrRequestProfileId,
         &SettingsStore::setRawPageOcrRequestProfileId,
         &SettingsStore::rawPageOcrModelName,
         &SettingsStore::setRawPageOcrModelName},
        // BlockRecognition: the settings keep the legacy "check" prefix.
        {{&SettingsStore::checkLaunchModelPath, &SettingsStore::setCheckLaunchModelPath},
         {&SettingsStore::checkLaunchMmprojPath, &SettingsStore::setCheckLaunchMmprojPath},
         {&SettingsStore::checkLaunchDraftPath, &SettingsStore::setCheckLaunchDraftPath},
         &SettingsStore::checkRequestProfileId,
         &SettingsStore::setCheckRequestProfileId,
         &SettingsStore::checkModelName,
         &SettingsStore::setCheckModelName},
        {{&SettingsStore::decisionLaunchModelPath, &SettingsStore::setDecisionLaunchModelPath},
         {&SettingsStore::decisionLaunchMmprojPath, &SettingsStore::setDecisionLaunchMmprojPath},
         {&SettingsStore::decisionLaunchDraftPath, &SettingsStore::setDecisionLaunchDraftPath},
         &SettingsStore::decisionRequestProfileId,
         &SettingsStore::setDecisionRequestProfileId,
         &SettingsStore::decisionModelName,
         &SettingsStore::setDecisionModelName},
        {{&SettingsStore::layoutLaunchModelPath, &SettingsStore::setLayoutLaunchModelPath},
         {&SettingsStore::layoutLaunchMmprojPath, &SettingsStore::setLayoutLaunchMmprojPath},
         {&SettingsStore::layoutLaunchDraftPath, &SettingsStore::setLayoutLaunchDraftPath},
         &SettingsStore::layoutRequestProfileId,
         &SettingsStore::setLayoutRequestProfileId,
         &SettingsStore::layoutModelName,
         &SettingsStore::setLayoutModelName},
        {{&SettingsStore::fixLaunchModelPath, &SettingsStore::setFixLaunchModelPath},
         {&SettingsStore::fixLaunchMmprojPath, &SettingsStore::setFixLaunchMmprojPath},
         {&SettingsStore::fixLaunchDraftPath, &SettingsStore::setFixLaunchDraftPath},
         &SettingsStore::fixRequestProfileId,
         &SettingsStore::setFixRequestProfileId,
         &SettingsStore::fixModelName,
         &SettingsStore::setFixModelName},
    };
    static_assert(std::size(table) == connectionRoleCount, "the role table must cover every role");
    return table[static_cast<std::size_t>(role)];
}

QString defaultBlockRecognitionRequestProfileId()
{
    return ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("blockRecognition"));
}

QString defaultDecisionRequestProfileId()
{
    return ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("decision"));
}

QString defaultLayoutRequestProfileId()
{
    return ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("layout"));
}

QString defaultRawPageOcrRequestProfileId()
{
    return ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("rawPageOcr"));
}

QString defaultFixRequestProfileId()
{
    return ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("fix"));
}

}  // namespace

const QList<SettingsStore::SettingDefault> &SettingsStore::defaultTable()
{
    // Built once, on first use: the check role's default is the id of a model
    // profile, and the catalog lives in a resource that is not linked into every
    // binary that reads the settings.
    static const QList<SettingDefault> table = {
        {kLanguage, "language", QVariant(QString::fromUtf8(kDefaultLanguage))},
        {kThemeMode, "themeMode", QVariant(kDefaultThemeMode)},
        {kBaseUrl, "baseUrl", QVariant(QString::fromUtf8(kDefaultBaseUrl))},
        {kApiKey, "apiKey", QVariant(QString::fromUtf8(kDefaultApiKey))},
        {kConnectionTimeoutKey, "connectionTimeoutMs", QVariant(kDefaultConnectionTimeoutMs)},
        {kResponseTimeoutKey, "responseTimeoutMs", QVariant(kDefaultResponseTimeoutMs)},
        {kModelName, "modelName", QVariant(QString::fromUtf8(kDefaultModelName))},
        {kModelRecipeId, "modelRecipeId", QVariant(QString::fromUtf8(kDefaultModelRecipeId))},
        {kParserId, "parserId", QVariant(QString::fromUtf8(kDefaultParserId))},
        {kOrientationCheck, "orientationCheckEnabled", QVariant(QString::fromUtf8(kDefaultOrientationCheck))},
        {kSplitPages, "splitPages", QVariant(kDefaultSplitPages)},
        {kKeepPageNumbers, "keepPageNumbers", QVariant(kDefaultKeepPageNumbers)},
        {kTablesAsHtml, "tablesAsHtml", QVariant(kDefaultTablesAsHtml)},
        {kPdfLandscape, "pdfLandscape", QVariant(kDefaultPdfLandscape)},
        {kPdfMarginMm, "pdfMarginMm", QVariant(kDefaultPdfMarginMm)},
        {kConnectionMode, "connectionMode", QVariant(QString::fromUtf8(kModeExternal))},
        {kSetupVersion, "setupVersion", QVariant(0)},  // 0 = re-run first-run wizard
        {kSetupDismissed, "setupDismissed", QVariant(false)},
        {kServerPath, "serverPath", QVariant(QString())},
        {kRuntimeRootDir, "runtimeRootDir", QVariant(QString())},
        {kRuntimeModelsDir, "runtimeModelsDir", QVariant(QString())},
        {kRuntimeBackend, "runtimeBackend", QVariant(QString())},
        {kInstalledBuild, "installedBuild", QVariant(QString())},
        {kAutoStart, "autoStart", QVariant(false)},
        {kStartOnDemand, "startOnDemand", QVariant(true)},
        {kStopOnExit, "stopOnExit", QVariant(true)},
        {kAutoRestart, "autoRestart", QVariant(true)},
        {kStartupTimeoutMs, "startupTimeoutMs", QVariant(kDefaultStartupTimeoutMs)},
        {kAllowNonLoopback, "allowNonLoopback", QVariant(false)},
        {kLaunchProfileId, "launchProfileId", QVariant(QString())},
        {kLaunchModelPath, "launchModelPath", QVariant(QString())},
        {kLaunchMmprojPath, "launchMmprojPath", QVariant(QString())},
        {kLaunchDraftPath, "launchDraftPath", QVariant(QString())},
        {kLaunchHost, "launchHost", QVariant(QString::fromUtf8(kDefaultHost))},
        {kLaunchPort, "launchPort", QVariant(kDefaultPort)},
        {kCheckLaunchModelPath, "checkLaunchModelPath", QVariant(QString())},
        {kCheckLaunchMmprojPath, "checkLaunchMmprojPath", QVariant(QString())},
        {kCheckLaunchDraftPath, "checkLaunchDraftPath", QVariant(QString())},
        {kCheckRequestProfileId, "checkRequestProfileId", QVariant(defaultBlockRecognitionRequestProfileId())},
        {kCheckModelName, "checkModelName", QVariant(QString())},
        {kShowConfidence, "showConfidence", QVariant(false)},
        {kDecisionLaunchModelPath, "decisionLaunchModelPath", QVariant(QString())},
        {kDecisionLaunchMmprojPath, "decisionLaunchMmprojPath", QVariant(QString())},
        {kDecisionLaunchDraftPath, "decisionLaunchDraftPath", QVariant(QString())},
        {kDecisionRequestProfileId, "decisionRequestProfileId", QVariant(defaultDecisionRequestProfileId())},
        {kDecisionModelName, "decisionModelName", QVariant(QString())},
        {kDecisionMatchThreshold, "decisionMatchThreshold", QVariant(kDefaultDecisionMatchThreshold)},
        {kLayoutLaunchModelPath, "layoutLaunchModelPath", QVariant(QString())},
        {kLayoutLaunchMmprojPath, "layoutLaunchMmprojPath", QVariant(QString())},
        {kLayoutLaunchDraftPath, "layoutLaunchDraftPath", QVariant(QString())},
        {kLayoutRequestProfileId, "layoutRequestProfileId", QVariant(defaultLayoutRequestProfileId())},
        {kLayoutModelName, "layoutModelName", QVariant(QString())},
        {kRawPageOcrLaunchModelPath, "rawPageOcrLaunchModelPath", QVariant(QString())},
        {kRawPageOcrLaunchMmprojPath, "rawPageOcrLaunchMmprojPath", QVariant(QString())},
        {kRawPageOcrLaunchDraftPath, "rawPageOcrLaunchDraftPath", QVariant(QString())},
        {kRawPageOcrRequestProfileId, "rawPageOcrRequestProfileId", QVariant(defaultRawPageOcrRequestProfileId())},
        {kRawPageOcrModelName, "rawPageOcrModelName", QVariant(QString())},
        {kFixLaunchModelPath, "fixLaunchModelPath", QVariant(QString())},
        {kFixLaunchMmprojPath, "fixLaunchMmprojPath", QVariant(QString())},
        {kFixLaunchDraftPath, "fixLaunchDraftPath", QVariant(QString())},
        {kFixRequestProfileId, "fixRequestProfileId", QVariant(defaultFixRequestProfileId())},
        {kFixModelName, "fixModelName", QVariant(QString())},
        {kHfToken, "hfToken", QVariant(QString())},
        {kLastExternalBaseUrl, "lastExternalBaseUrl", QVariant(QString())},
    };
    return table;
}

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
        const bool looksConfigured = m_settings.contains(kBaseUrl) && !m_settings.value(kBaseUrl).toString().isEmpty();
        m_settings.setValue(kConnectionMode, QString::fromUtf8(kModeExternal));
        m_settings.setValue(kSetupVersion, looksConfigured ? kCurrentSetupVersion : 0);
    }

    if (m_settings.value(kConnectionMode).toString() == QString::fromUtf8(kModeExternal) && m_settings.contains(kBaseUrl) && m_settings.value(kBaseUrl).toString().isEmpty()) {
        const QString saved = m_settings.value(kLastExternalBaseUrl).toString();
        if (!saved.isEmpty())
            m_settings.setValue(kBaseUrl, saved);
    }

    for (const char *retired : {kRetiredAlias,
                                kRetiredServerPathIsManaged,
                                kRetiredLaunchSourceDownload,
                                kRetiredCheckSourceDownload,
                                kRetiredModelRequestProfileId,
                                kRetiredAutoCheck,
                                kRetiredAutoFix,
                                kRetiredAutoRecheck})
        m_settings.remove(QString::fromLatin1(retired));

    // `provider/timeoutMs` capped the whole request, so its value lives on as
    // the response timeout; the connection timeout starts from its own default.
    if (!m_settings.contains(kResponseTimeoutKey) && m_settings.contains(kRetiredTimeoutMs)) {
        const int legacy = m_settings.value(kRetiredTimeoutMs).toInt();
        if (legacy > 0)
            m_settings.setValue(kResponseTimeoutKey, legacy);
    }
    m_settings.remove(QString::fromUtf8(kRetiredTimeoutMs));

    // The ex-role model keys move into the tasks/<task> namespace; the value
    // survives under the new key and the legacy key is removed. The pageOcr
    // launch profile id (launch/profileId) and host/port stay shared.
    const QList<std::pair<const char *, const char *>> taskKeyMoves = {
        {kRetiredPageOcrModelPath, kLaunchModelPath},
        {kRetiredPageOcrMmprojPath, kLaunchMmprojPath},
        {kRetiredPageOcrDraftPath, kLaunchDraftPath},
        {kRetiredPageOcrRequestProfileId, kModelRecipeId},
        {kRetiredCheckModelPath, kCheckLaunchModelPath},
        {kRetiredCheckMmprojPath, kCheckLaunchMmprojPath},
        {kRetiredCheckDraftPath, kCheckLaunchDraftPath},
        {kRetiredCheckRequestProfileId, kCheckRequestProfileId},
        {kRetiredCheckModelName, kCheckModelName},
        {kRetiredDecisionModelPath, kDecisionLaunchModelPath},
        {kRetiredDecisionMmprojPath, kDecisionLaunchMmprojPath},
        {kRetiredDecisionDraftPath, kDecisionLaunchDraftPath},
        {kRetiredDecisionRequestProfileId, kDecisionRequestProfileId},
        {kRetiredDecisionModelName, kDecisionModelName},
        {kRetiredLayoutModelPath, kLayoutLaunchModelPath},
        {kRetiredLayoutMmprojPath, kLayoutLaunchMmprojPath},
        {kRetiredLayoutDraftPath, kLayoutLaunchDraftPath},
        {kRetiredLayoutRequestProfileId, kLayoutRequestProfileId},
        {kRetiredLayoutModelName, kLayoutModelName},
    };
    for (const auto &move : taskKeyMoves) {
        const QString from = QString::fromLatin1(move.first);
        const QString to = QString::fromLatin1(move.second);
        if (!m_settings.contains(from))
            continue;
        if (!m_settings.contains(to))
            m_settings.setValue(to, m_settings.value(from));
        m_settings.remove(from);
    }

    resolveStoredModelId(kModelRecipeId, QStringLiteral("pageOcr"));
    resolveStoredModelId(kCheckRequestProfileId, QStringLiteral("blockRecognition"));
    resolveStoredModelId(kDecisionRequestProfileId, QStringLiteral("decision"));
    resolveStoredModelId(kLayoutRequestProfileId, QStringLiteral("layout"));
    resolveStoredModelId(kRawPageOcrRequestProfileId, QStringLiteral("rawPageOcr"));
    resolveStoredModelId(kFixRequestProfileId, QStringLiteral("fix"));

    if (m_settings.value(kParserId).toString() == QLatin1String("det_tokens")) {
        QString resolved = QString::fromUtf8(kDefaultParserId);
        if (const ModelProfiles::Role *role = ModelProfiles::roleFor(modelRecipeId(), QStringLiteral("pageOcr"))) {
            if (!role->parser.isEmpty())
                resolved = role->parser;
        }
        m_settings.setValue(kParserId, resolved);
    }
}

void SettingsStore::resolveStoredModelId(const char *key, const QString &role)
{
    const QString stored = m_settings.value(key).toString();
    if (stored.isEmpty() || ModelProfiles::roleFor(stored, role))
        return;
    const QString resolved = ModelProfiles::defaultIdForRole(ModelProfiles::instance(), role);
    if (resolved.isEmpty() || resolved == stored)
        return;
    m_settings.setValue(key, resolved);
}

void SettingsStore::forceSave()
{
    m_settings.sync();
}

void SettingsStore::resetToDefaults()
{
    resetGroup([](const QString &) { return true; });
}

void SettingsStore::resetGroup(const std::function<bool(const QString &)> &matches)
{
    const QMetaObject *mo = metaObject();
    for (const SettingDefault &entry : defaultTable()) {
        if (!matches(QString::fromUtf8(entry.key)))
            continue;
        const QMetaProperty prop = mo->property(mo->indexOfProperty(entry.property));
        if (!prop.isValid() || !prop.write(this, entry.defaultValue)) {
            qWarning("SettingsStore: resetGroup() cannot write property %s", entry.property);
        }
    }
    forceSave();
}

void SettingsStore::resetOutputDefaults()
{
    resetGroup([](const QString &key) { return key == QLatin1String("parser/id") || key.startsWith(QLatin1String("output/")) || key.startsWith(QLatin1String("export/")); });
}

void SettingsStore::resetRuntimeDefaults()
{
    resetGroup([](const QString &key) {
        return key == QLatin1String("provider/mode") || key == QLatin1String("provider/baseUrl") || key == QLatin1String("provider/apiKey") || key == QLatin1String("provider/connectionTimeoutMs") ||
               key == QLatin1String("provider/responseTimeoutMs") || key == QLatin1String("model/name") || key == QLatin1String("tasks/blockRecognition/modelName") ||
               key == QLatin1String("tasks/decision/modelName") || key == QLatin1String("tasks/layout/modelName") || key == QLatin1String("tasks/rawPageOcr/modelName") ||
               key == QLatin1String("tasks/fix/modelName") || key == QLatin1String("runtime/serverPath");
    });
}

const SettingsStore::SettingDefault *SettingsStore::defaults()
{
    return defaultTable().constData();
}

int SettingsStore::defaultsCount()
{
    return int(defaultTable().size());
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
    return std::clamp(m_settings.value(kConnectionTimeoutKey, kDefaultConnectionTimeoutMs).toInt(), kMinTimeoutMs, kMaxConnectionTimeoutMs);
}

void SettingsStore::setConnectionTimeoutMs(int ms)
{
    ms = std::clamp(ms, kMinTimeoutMs, kMaxConnectionTimeoutMs);
    if (connectionTimeoutMs() == ms)
        return;
    m_settings.setValue(kConnectionTimeoutKey, ms);
    emit connectionTimeoutMsChanged();
}

int SettingsStore::responseTimeoutMs() const
{
    return std::clamp(m_settings.value(kResponseTimeoutKey, kDefaultResponseTimeoutMs).toInt(), kMinTimeoutMs, kMaxResponseTimeoutMs);
}

void SettingsStore::setResponseTimeoutMs(int ms)
{
    ms = std::clamp(ms, kMinTimeoutMs, kMaxResponseTimeoutMs);
    if (responseTimeoutMs() == ms)
        return;
    m_settings.setValue(kResponseTimeoutKey, ms);
    emit responseTimeoutMsChanged();
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

void SettingsStore::selectModelProfile(const QString &repo, const QString &role)
{
    const QString profileId = ModelProfiles::idForRepo(ModelProfiles::instance(), repo);
    if (profileId.isEmpty() || !ModelProfiles::roleFor(profileId, role))
        return;
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    (this->*settings.setRequestProfileId)(profileId);
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

bool SettingsStore::orientationCheckEnabled() const
{
    return m_settings.value(kOrientationCheck, kDefaultOrientationCheck).toBool();
}

void SettingsStore::setOrientationCheckEnabled(bool on)
{
    if (orientationCheckEnabled() == on)
        return;
    m_settings.setValue(kOrientationCheck, on);
    emit orientationCheckEnabledChanged();
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

bool SettingsStore::tablesAsHtml() const
{
    return m_settings.value(kTablesAsHtml, kDefaultTablesAsHtml).toBool();
}

void SettingsStore::setTablesAsHtml(bool on)
{
    if (tablesAsHtml() == on)
        return;
    m_settings.setValue(kTablesAsHtml, on);
    emit tablesAsHtmlChanged();
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
    mm = std::clamp(mm, 0, kMaxPdfMarginMm);
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
    return connectionMode() == QString::fromUtf8(kModeManaged) ? ConnectionMode::Managed : ConnectionMode::External;
}

void SettingsStore::setMode(ConnectionMode mode)
{
    setConnectionMode(mode == ConnectionMode::Managed ? QString::fromUtf8(kModeManaged) : QString::fromUtf8(kModeExternal));
}

void SettingsStore::setConnectionMode(const QString &mode)
{
    if (mode != QString::fromUtf8(kModeExternal) && mode != QString::fromUtf8(kModeManaged)) {
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
    return std::clamp(m_settings.value(kStartupTimeoutMs, kDefaultStartupTimeoutMs).toInt(), kMinTimeoutMs, kMaxStartupTimeoutMs);
}

void SettingsStore::setStartupTimeoutMs(int ms)
{
    ms = std::clamp(ms, kMinTimeoutMs, kMaxStartupTimeoutMs);
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

void SettingsStore::setLaunchProfileId(const QString &id)
{
    if (launchProfileId() == id)
        return;
    m_settings.setValue(kLaunchProfileId, id);
    emit launchProfileIdChanged();
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

QString SettingsStore::launchDraftPath() const
{
    return m_settings.value(kLaunchDraftPath).toString();
}

void SettingsStore::setLaunchDraftPath(const QString &path)
{
    if (launchDraftPath() == path)
        return;
    m_settings.setValue(kLaunchDraftPath, path);
    emit launchDraftPathChanged();
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
    return std::clamp(m_settings.value(kLaunchPort, kDefaultPort).toInt(), 0, kMaxPort);
}

void SettingsStore::setLaunchPort(int port)
{
    port = std::clamp(port, 0, kMaxPort);
    if (launchPort() == port)
        return;
    m_settings.setValue(kLaunchPort, port);
    emit launchPortChanged();
}

QString SettingsStore::checkLaunchModelPath() const
{
    return m_settings.value(kCheckLaunchModelPath).toString();
}

void SettingsStore::setCheckLaunchModelPath(const QString &path)
{
    if (checkLaunchModelPath() == path)
        return;
    m_settings.setValue(kCheckLaunchModelPath, path);
    emit checkLaunchModelPathChanged();
}

QString SettingsStore::checkLaunchMmprojPath() const
{
    return m_settings.value(kCheckLaunchMmprojPath).toString();
}

void SettingsStore::setCheckLaunchMmprojPath(const QString &path)
{
    if (checkLaunchMmprojPath() == path)
        return;
    m_settings.setValue(kCheckLaunchMmprojPath, path);
    emit checkLaunchMmprojPathChanged();
}

QString SettingsStore::checkLaunchDraftPath() const
{
    return m_settings.value(kCheckLaunchDraftPath).toString();
}

void SettingsStore::setCheckLaunchDraftPath(const QString &path)
{
    if (checkLaunchDraftPath() == path)
        return;
    m_settings.setValue(kCheckLaunchDraftPath, path);
    emit checkLaunchDraftPathChanged();
}

QString SettingsStore::checkRequestProfileId() const
{
    const QString stored = m_settings.value(kCheckRequestProfileId).toString();
    return stored.isEmpty() ? defaultBlockRecognitionRequestProfileId() : stored;
}

void SettingsStore::setCheckRequestProfileId(const QString &id)
{
    if (checkRequestProfileId() == id)
        return;
    m_settings.setValue(kCheckRequestProfileId, id);
    emit checkRequestProfileIdChanged();
}

QString SettingsStore::checkModelName() const
{
    return m_settings.value(kCheckModelName).toString();
}

void SettingsStore::setCheckModelName(const QString &name)
{
    if (checkModelName() == name)
        return;
    m_settings.setValue(kCheckModelName, name);
    emit checkModelNameChanged();
}

bool SettingsStore::showConfidence() const
{
    return m_settings.value(kShowConfidence, false).toBool();
}

void SettingsStore::setShowConfidence(bool on)
{
    if (showConfidence() == on)
        return;
    m_settings.setValue(kShowConfidence, on);
    emit showConfidenceChanged();
}

QString SettingsStore::decisionLaunchModelPath() const
{
    return m_settings.value(kDecisionLaunchModelPath).toString();
}

void SettingsStore::setDecisionLaunchModelPath(const QString &path)
{
    if (decisionLaunchModelPath() == path)
        return;
    m_settings.setValue(kDecisionLaunchModelPath, path);
    emit decisionLaunchModelPathChanged();
}

QString SettingsStore::decisionLaunchMmprojPath() const
{
    return m_settings.value(kDecisionLaunchMmprojPath).toString();
}

void SettingsStore::setDecisionLaunchMmprojPath(const QString &path)
{
    if (decisionLaunchMmprojPath() == path)
        return;
    m_settings.setValue(kDecisionLaunchMmprojPath, path);
    emit decisionLaunchMmprojPathChanged();
}

QString SettingsStore::decisionLaunchDraftPath() const
{
    return m_settings.value(kDecisionLaunchDraftPath).toString();
}

void SettingsStore::setDecisionLaunchDraftPath(const QString &path)
{
    if (decisionLaunchDraftPath() == path)
        return;
    m_settings.setValue(kDecisionLaunchDraftPath, path);
    emit decisionLaunchDraftPathChanged();
}

QString SettingsStore::decisionRequestProfileId() const
{
    const QString stored = m_settings.value(kDecisionRequestProfileId).toString();
    return stored.isEmpty() ? defaultDecisionRequestProfileId() : stored;
}

void SettingsStore::setDecisionRequestProfileId(const QString &id)
{
    if (decisionRequestProfileId() == id)
        return;
    m_settings.setValue(kDecisionRequestProfileId, id);
    emit decisionRequestProfileIdChanged();
}

QString SettingsStore::decisionModelName() const
{
    return m_settings.value(kDecisionModelName).toString();
}

void SettingsStore::setDecisionModelName(const QString &name)
{
    if (decisionModelName() == name)
        return;
    m_settings.setValue(kDecisionModelName, name);
    emit decisionModelNameChanged();
}

double SettingsStore::defaultDecisionMatchThreshold() const
{
    return kDefaultDecisionMatchThreshold;
}

double SettingsStore::decisionMatchThreshold() const
{
    const double v = m_settings.value(kDecisionMatchThreshold, kDefaultDecisionMatchThreshold).toDouble();
    return std::clamp(v, 0.0, 1.0);
}

void SettingsStore::setDecisionMatchThreshold(double threshold)
{
    threshold = std::clamp(threshold, 0.0, 1.0);
    if (decisionMatchThreshold() == threshold)
        return;
    m_settings.setValue(kDecisionMatchThreshold, threshold);
    emit decisionMatchThresholdChanged();
}

QString SettingsStore::layoutLaunchModelPath() const
{
    return m_settings.value(kLayoutLaunchModelPath).toString();
}

void SettingsStore::setLayoutLaunchModelPath(const QString &path)
{
    if (layoutLaunchModelPath() == path)
        return;
    m_settings.setValue(kLayoutLaunchModelPath, path);
    emit layoutLaunchModelPathChanged();
}

QString SettingsStore::layoutLaunchMmprojPath() const
{
    return m_settings.value(kLayoutLaunchMmprojPath).toString();
}

void SettingsStore::setLayoutLaunchMmprojPath(const QString &path)
{
    if (layoutLaunchMmprojPath() == path)
        return;
    m_settings.setValue(kLayoutLaunchMmprojPath, path);
    emit layoutLaunchMmprojPathChanged();
}

QString SettingsStore::layoutLaunchDraftPath() const
{
    return m_settings.value(kLayoutLaunchDraftPath).toString();
}

void SettingsStore::setLayoutLaunchDraftPath(const QString &path)
{
    if (layoutLaunchDraftPath() == path)
        return;
    m_settings.setValue(kLayoutLaunchDraftPath, path);
    emit layoutLaunchDraftPathChanged();
}

QString SettingsStore::layoutRequestProfileId() const
{
    const QString stored = m_settings.value(kLayoutRequestProfileId).toString();
    return stored.isEmpty() ? defaultLayoutRequestProfileId() : stored;
}

void SettingsStore::setLayoutRequestProfileId(const QString &id)
{
    if (layoutRequestProfileId() == id)
        return;
    m_settings.setValue(kLayoutRequestProfileId, id);
    emit layoutRequestProfileIdChanged();
}

QString SettingsStore::layoutModelName() const
{
    return m_settings.value(kLayoutModelName).toString();
}

void SettingsStore::setLayoutModelName(const QString &name)
{
    if (layoutModelName() == name)
        return;
    m_settings.setValue(kLayoutModelName, name);
    emit layoutModelNameChanged();
}

QString SettingsStore::rawPageOcrLaunchModelPath() const
{
    return m_settings.value(kRawPageOcrLaunchModelPath).toString();
}

void SettingsStore::setRawPageOcrLaunchModelPath(const QString &path)
{
    if (rawPageOcrLaunchModelPath() == path)
        return;
    m_settings.setValue(kRawPageOcrLaunchModelPath, path);
    emit rawPageOcrLaunchModelPathChanged();
}

QString SettingsStore::rawPageOcrLaunchMmprojPath() const
{
    return m_settings.value(kRawPageOcrLaunchMmprojPath).toString();
}

void SettingsStore::setRawPageOcrLaunchMmprojPath(const QString &path)
{
    if (rawPageOcrLaunchMmprojPath() == path)
        return;
    m_settings.setValue(kRawPageOcrLaunchMmprojPath, path);
    emit rawPageOcrLaunchMmprojPathChanged();
}

QString SettingsStore::rawPageOcrLaunchDraftPath() const
{
    return m_settings.value(kRawPageOcrLaunchDraftPath).toString();
}

void SettingsStore::setRawPageOcrLaunchDraftPath(const QString &path)
{
    if (rawPageOcrLaunchDraftPath() == path)
        return;
    m_settings.setValue(kRawPageOcrLaunchDraftPath, path);
    emit rawPageOcrLaunchDraftPathChanged();
}

QString SettingsStore::rawPageOcrRequestProfileId() const
{
    const QString stored = m_settings.value(kRawPageOcrRequestProfileId).toString();
    return stored.isEmpty() ? defaultRawPageOcrRequestProfileId() : stored;
}

void SettingsStore::setRawPageOcrRequestProfileId(const QString &id)
{
    if (rawPageOcrRequestProfileId() == id)
        return;
    m_settings.setValue(kRawPageOcrRequestProfileId, id);
    emit rawPageOcrRequestProfileIdChanged();
}

QString SettingsStore::rawPageOcrModelName() const
{
    return m_settings.value(kRawPageOcrModelName).toString();
}

void SettingsStore::setRawPageOcrModelName(const QString &name)
{
    if (rawPageOcrModelName() == name)
        return;
    m_settings.setValue(kRawPageOcrModelName, name);
    emit rawPageOcrModelNameChanged();
}

QString SettingsStore::fixLaunchModelPath() const
{
    return m_settings.value(kFixLaunchModelPath).toString();
}

void SettingsStore::setFixLaunchModelPath(const QString &path)
{
    if (fixLaunchModelPath() == path)
        return;
    m_settings.setValue(kFixLaunchModelPath, path);
    emit fixLaunchModelPathChanged();
}

QString SettingsStore::fixLaunchMmprojPath() const
{
    return m_settings.value(kFixLaunchMmprojPath).toString();
}

void SettingsStore::setFixLaunchMmprojPath(const QString &path)
{
    if (fixLaunchMmprojPath() == path)
        return;
    m_settings.setValue(kFixLaunchMmprojPath, path);
    emit fixLaunchMmprojPathChanged();
}

QString SettingsStore::fixLaunchDraftPath() const
{
    return m_settings.value(kFixLaunchDraftPath).toString();
}

void SettingsStore::setFixLaunchDraftPath(const QString &path)
{
    if (fixLaunchDraftPath() == path)
        return;
    m_settings.setValue(kFixLaunchDraftPath, path);
    emit fixLaunchDraftPathChanged();
}

QString SettingsStore::fixRequestProfileId() const
{
    const QString stored = m_settings.value(kFixRequestProfileId).toString();
    return stored.isEmpty() ? defaultFixRequestProfileId() : stored;
}

void SettingsStore::setFixRequestProfileId(const QString &id)
{
    if (fixRequestProfileId() == id)
        return;
    m_settings.setValue(kFixRequestProfileId, id);
    emit fixRequestProfileIdChanged();
}

QString SettingsStore::fixModelName() const
{
    return m_settings.value(kFixModelName).toString();
}

void SettingsStore::setFixModelName(const QString &name)
{
    if (fixModelName() == name)
        return;
    m_settings.setValue(kFixModelName, name);
    emit fixModelNameChanged();
}

QString SettingsStore::launchModelPathForRole(const QString &role) const
{
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    return (this->*settings.modelPath.read)();
}

void SettingsStore::setLaunchModelPathForRole(const QString &role, const QString &path)
{
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    (this->*settings.modelPath.write)(path);
}

QString SettingsStore::launchMmprojPathForRole(const QString &role) const
{
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    return (this->*settings.mmprojPath.read)();
}

void SettingsStore::setLaunchMmprojPathForRole(const QString &role, const QString &path)
{
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    (this->*settings.mmprojPath.write)(path);
}

QString SettingsStore::launchDraftPathForRole(const QString &role) const
{
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    return (this->*settings.draftPath.read)();
}

void SettingsStore::setLaunchDraftPathForRole(const QString &role, const QString &path)
{
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    (this->*settings.draftPath.write)(path);
}

QString SettingsStore::requestProfileIdForRole(const QString &role) const
{
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    return (this->*settings.requestProfileId)();
}

QString SettingsStore::modelNameForRole(const QString &role) const
{
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    return (this->*settings.modelName)();
}

void SettingsStore::setModelNameForRole(const QString &role, const QString &name)
{
    const RoleSettings &settings = roleSettings(connectionRoleFromName(role));
    (this->*settings.setModelName)(name);
}

QStringList SettingsStore::taskRoles() const
{
    return connectionRoleIds();
}

QString SettingsStore::launchProfileId() const
{
    return m_settings.value(kLaunchProfileId).toString();
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

QString SettingsStore::selectedQuant(const QString &profileId) const
{
    if (profileId.isEmpty())
        return QString();
    return m_settings.value(QLatin1String(kQuantSelectionGroup) + QLatin1Char('/') + profileId).toString();
}

void SettingsStore::setSelectedQuant(const QString &profileId, const QString &quantId)
{
    if (profileId.isEmpty())
        return;
    const QString key = QLatin1String(kQuantSelectionGroup) + QLatin1Char('/') + profileId;
    if (quantId.isEmpty())
        m_settings.remove(key);
    else
        m_settings.setValue(key, quantId);
}

}  // namespace llocr
