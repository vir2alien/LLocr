// The one settings window: five sections, one Save and one Cancel for all of
// them, the setup wizard and the reset in the footer (ADR 297).
//
// The window is the first thing the user opens when something has to be
// changed, and it is the only place where the live sections (the theme, the
// language, the block types) are applied while the window is open. The test
// pins that behaviour: the sections, the switch between them, the undo the
// Cancel owes the user, the reset that writes at once, and the wizard request
// that closes the window.
//
// The runtime, the installers and the profile stores are stand-ins here: the
// test is about the window, and the real ones would start processes and touch
// the user's disk.

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtTest>

#include "app/I18n.h"
#include "app/UiController.h"
#include "app/VerificationPromptStore.h"
#include "config/SettingsStore.h"
#include "runtime/ConnectionProbe.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A label with this text somewhere under the object.
bool hasLabelWithText(QObject *root, const QString &text)
{
    if (!root)
        return false;
    if (root->property("text").toString() == text)
        return true;
    if (auto *item = qobject_cast<QQuickItem *>(root)) {
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children) {
            if (hasLabelWithText(child, text))
                return true;
        }
    }
    // A scroll view keeps its content in the content item, not in the children.
    QObject *content = root->property("contentItem").value<QObject *>();
    if (content && content != root)
        return hasLabelWithText(content, text);
    return false;
}

// The text of the first label under the object that starts with this prefix.
QString labelTextStartingWith(QObject *root, const QString &prefix)
{
    if (!root)
        return QString();
    if (root->property("text").toString().startsWith(prefix))
        return root->property("text").toString();
    if (auto *item = qobject_cast<QQuickItem *>(root)) {
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children) {
            const QString text = labelTextStartingWith(child, prefix);
            if (!text.isEmpty())
                return text;
        }
    }
    // A scroll view keeps its content in the content item, not in the children.
    QObject *content = root->property("contentItem").value<QObject *>();
    if (content && content != root)
        return labelTextStartingWith(content, prefix);
    return QString();
}

// The first item under the object that carries this text. Its visibility
// tells the state of the card: the rows of the update card are all in the
// tree, only the one of the current state is shown.
QQuickItem *findItemWithText(QObject *root, const QString &text)
{
    if (!root)
        return nullptr;
    if (root->property("text").toString() == text) {
        if (auto *item = qobject_cast<QQuickItem *>(root))
            return item;
    }
    if (auto *item = qobject_cast<QQuickItem *>(root)) {
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children) {
            if (QQuickItem *found = findItemWithText(child, text))
                return found;
        }
    }
    QObject *content = root->property("contentItem").value<QObject *>();
    if (content && content != root)
        return findItemWithText(content, text);
    return nullptr;
}

// The runtime tab itself: the QML type carries the suffix the module gives it.
QQuickItem *findRuntimeTab(QQuickItem *root)
{
    for (QQuickItem *child : root->childItems()) {
        if (QString::fromLatin1(child->metaObject()->className()).startsWith(QLatin1String("RuntimeTab_QMLTYPE")))
            return child;
        if (QQuickItem *found = findRuntimeTab(child))
            return found;
    }
    return nullptr;
}

// A visible hairline directly under the object — the divider between the
// connection settings and the blocks is gone. The underline of a text field is
// deeper in the tree and does not count.
bool hasHairlineChild(QObject *parent)
{
    auto *item = qobject_cast<QQuickItem *>(parent);
    if (!item)
        return false;
    for (QQuickItem *child : item->childItems()) {
        if (child->isVisible() && child->height() > 0 && child->height() < 2 && child->width() > 100)
            return true;
    }
    return false;
}

// The list of models the table shows: no rows, so every row falls back to
// "no model chosen".
class FakeQuantModel : public QAbstractListModel
{
    Q_OBJECT

public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent)
        return 0;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        Q_UNUSED(index)
        Q_UNUSED(role)
        return {};
    }

    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, QByteArrayLiteral("key")}, {Qt::UserRole + 2, QByteArrayLiteral("title")}}; }

    Q_INVOKABLE QVariantMap rowFor(const QString &key) const
    {
        Q_UNUSED(key)
        QVariantMap row;
        row.insert(QStringLiteral("quants"), QVariantList());
        row.insert(QStringLiteral("selected"), -1);
        return row;
    }

    Q_INVOKABLE int rowIndexOf(const QString &key) const
    {
        Q_UNUSED(key)
        return -1;
    }

    explicit FakeQuantModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
};

// The stand-in for the app controller: the output section asks it for the
// parsers it can offer.
class FakeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList parserNames READ parserNames CONSTANT)
    Q_PROPERTY(QStringList parserLabels READ parserLabels CONSTANT)

public:
    QStringList parserNames() const { return {QStringLiteral("auto")}; }
    QStringList parserLabels() const { return {QStringLiteral("Auto")}; }
};

// The stand-in for the model installer: the models section asks it for the
// list, the queue and the files on disk.
class FakeModelInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state CONSTANT)
    Q_PROPERTY(bool busy READ busy CONSTANT)
    Q_PROPERTY(double progress READ progress CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage CONSTANT)
    Q_PROPERTY(bool taskQueueActive READ taskQueueActive CONSTANT)
    Q_PROPERTY(int taskQueueRemaining READ taskQueueRemaining CONSTANT)
    Q_PROPERTY(int installedCount READ installedCount CONSTANT)
    Q_PROPERTY(QObject *installedFiles READ installedFiles CONSTANT)
    Q_PROPERTY(QObject *allModels READ allModels CONSTANT)
    Q_PROPERTY(QObject *checkQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *decisionQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *layoutQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *fixQuantModels READ quantModels CONSTANT)

public:
    enum State { Idle = 0, Fetching = 1, ReadyToDownload = 2, Downloading = 3, Error = 4 };
    Q_ENUM(State)

    int state() const { return Idle; }
    bool busy() const { return false; }
    double progress() const { return 0.0; }
    QString statusMessage() const { return QString(); }
    bool taskQueueActive() const { return false; }
    int taskQueueRemaining() const { return 0; }
    int installedCount() const { return 0; }
    QObject *installedFiles() const { return m_models; }
    QObject *allModels() const { return m_models; }
    QObject *quantModels() const { return m_models; }

    Q_INVOKABLE void reloadPresets() {}
    Q_INVOKABLE void setDraftRuntimeModes(const QVariantMap &modes) { Q_UNUSED(modes) }
    Q_INVOKABLE QString setActiveModel(int index, const QString &task = QStringLiteral("blockRecognition"))
    {
        Q_UNUSED(index)
        Q_UNUSED(task)
        return QString();
    }
    Q_INVOKABLE QString removeModel(int index)
    {
        Q_UNUSED(index)
        return QString();
    }
    Q_INVOKABLE QString openModelFolder(int index)
    {
        Q_UNUSED(index)
        return QString();
    }
    Q_INVOKABLE void refreshInstalled() {}
    Q_INVOKABLE void rescanRegistry() {}
    Q_INVOKABLE void selectQuant(const QString &key, const QString &quantId)
    {
        Q_UNUSED(key)
        Q_UNUSED(quantId)
    }
    Q_INVOKABLE void downloadQuant(const QString &key, const QString &quantId, const QString &task = QString())
    {
        Q_UNUSED(key)
        Q_UNUSED(quantId)
        Q_UNUSED(task)
    }
    Q_INVOKABLE QString removeQuant(const QString &key, const QString &quantId, const QString &task = QStringLiteral("blockRecognition"))
    {
        Q_UNUSED(key)
        Q_UNUSED(quantId)
        Q_UNUSED(task)
        return QString();
    }
    Q_INVOKABLE QString removeModelRow(const QString &key, const QString &task = QStringLiteral("blockRecognition"))
    {
        Q_UNUSED(key)
        Q_UNUSED(task)
        return QString();
    }
    Q_INVOKABLE QString openQuantFolder(const QString &key, const QString &quantId, const QString &task = QStringLiteral("blockRecognition"))
    {
        Q_UNUSED(key)
        Q_UNUSED(quantId)
        Q_UNUSED(task)
        return QString();
    }
    Q_INVOKABLE QString removeInstalledEntry(const QString &entryId)
    {
        Q_UNUSED(entryId)
        return QString();
    }
    Q_INVOKABLE QString openInstalledEntryFolder(const QString &entryId)
    {
        Q_UNUSED(entryId)
        return QString();
    }
    Q_INVOKABLE void installPrepared() {}
    Q_INVOKABLE void cancelInstall() {}
    // The models that are not on disk: the save of the section fails while
    // the list is not empty.
    Q_INVOKABLE QVariantList applyTaskChoices(const QVariantList &choices)
    {
        Q_UNUSED(choices)
        return m_missing;
    }
    Q_INVOKABLE void queueTaskInstalls(const QVariantList &choices) { Q_UNUSED(choices) }
    Q_INVOKABLE QVariantList defaultTaskChoices(qint64 budgetBytes = -1)
    {
        Q_UNUSED(budgetBytes)
        return {};
    }
    Q_INVOKABLE void cancelTaskInstalls() {}

    void setMissingModels(const QVariantList &missing) { m_missing = missing; }

signals:
    void installedChanged();
    void presetsChanged();

private:
    FakeQuantModel *m_models = new FakeQuantModel(this);
    QVariantList m_missing;
};

// The stand-in for the runtime installer: the runtime section shows the
// backend, the release and the installed builds.
class FakeRuntimeInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy CONSTANT)
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)
    Q_PROPERTY(QString platformLabel READ platformLabel CONSTANT)
    Q_PROPERTY(QString recommendedBackend READ recommendedBackend CONSTANT)
    Q_PROPERTY(QString recommendationReason READ recommendationReason CONSTANT)
    Q_PROPERTY(QStringList availableBackends READ availableBackends CONSTANT)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(int releaseCount READ releaseCount CONSTANT)
    Q_PROPERTY(int selectedRelease READ selectedRelease WRITE setSelectedRelease NOTIFY selectedReleaseChanged)
    Q_PROPERTY(QString installedBuild READ installedBuild CONSTANT)
    Q_PROPERTY(QString installedBackend READ installedBackend CONSTANT)
    Q_PROPERTY(bool hasUpdate READ hasUpdate NOTIFY hasUpdateChanged)
    Q_PROPERTY(QString candidateTag READ candidateTag NOTIFY candidateChanged)
    Q_PROPERTY(QString candidateDate READ candidateDate NOTIFY candidateChanged)
    Q_PROPERTY(QString candidateSize READ candidateSize NOTIFY candidateChanged)
    Q_PROPERTY(bool candidateIgnored READ candidateIgnored NOTIFY candidateChanged)
    Q_PROPERTY(bool candidateInstalled READ candidateInstalled NOTIFY candidateChanged)
    Q_PROPERTY(QString ignoredBuild READ ignoredBuild NOTIFY candidateChanged)
    Q_PROPERTY(QString checkedAtLabel READ checkedAtLabel NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogWarning READ catalogWarning NOTIFY catalogChanged)
    Q_PROPERTY(int installedBuildCount READ installedBuildCount CONSTANT)
    Q_PROPERTY(QObject *installedBuilds READ installedBuilds CONSTANT)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage CONSTANT)
    Q_PROPERTY(bool canInstall READ canInstall CONSTANT)

public:
    enum State { Idle = 0, Fetching = 1, Ready = 2, Downloading = 3, Installing = 4, Installed = 5, Error = 6 };
    Q_ENUM(State)

    int state() const { return Idle; }
    bool busy() const { return false; }
    QString channel() const { return m_channel; }
    void setChannel(const QString &channel)
    {
        if (m_channel == channel)
            return;
        m_channel = channel;
        emit channelChanged();
    }
    QString platformLabel() const { return QStringLiteral("test"); }
    QString recommendedBackend() const { return QStringLiteral("cpu"); }
    QString recommendationReason() const { return QString(); }
    QStringList availableBackends() const { return {QStringLiteral("cpu")}; }
    QString backend() const { return m_backend; }
    void setBackend(const QString &backend)
    {
        if (m_backend == backend)
            return;
        m_backend = backend;
        emit backendChanged();
    }
    int releaseCount() const { return 1; }
    int selectedRelease() const { return m_selectedRelease; }
    void setSelectedRelease(int index)
    {
        if (m_selectedRelease == index)
            return;
        m_selectedRelease = index;
        emit selectedReleaseChanged();
    }
    QString installedBuild() const { return QStringLiteral("b12345"); }
    QString installedBackend() const { return QString(); }
    bool hasUpdate() const { return m_hasUpdate; }
    void setHasUpdate(bool value)
    {
        if (m_hasUpdate == value)
            return;
        m_hasUpdate = value;
        emit hasUpdateChanged();
    }
    QString candidateTag() const { return QStringLiteral("b12345"); }
    QString candidateDate() const { return QStringLiteral("2026-11-16"); }
    QString candidateSize() const { return QStringLiteral("649 MiB"); }
    bool candidateIgnored() const { return m_candidateIgnored; }
    void setCandidateIgnored(bool value)
    {
        if (m_candidateIgnored == value)
            return;
        m_candidateIgnored = value;
        emit candidateChanged();
    }
    bool candidateInstalled() const { return m_candidateInstalled; }
    void setCandidateInstalled(bool value)
    {
        if (m_candidateInstalled == value)
            return;
        m_candidateInstalled = value;
        emit candidateChanged();
    }
    QString ignoredBuild() const { return QString(); }
    QString checkedAtLabel() const { return QStringLiteral("09:23"); }
    QString catalogWarning() const { return m_catalogWarning; }
    void setCatalogWarning(const QString &value)
    {
        if (m_catalogWarning == value)
            return;
        m_catalogWarning = value;
        emit catalogChanged();
    }
    int installedBuildCount() const { return 0; }
    QObject *installedBuilds() const { return m_builds; }
    double progress() const { return 0.0; }
    QString statusMessage() const { return QString(); }
    bool canInstall() const { return true; }

    Q_INVOKABLE QString releaseLabel(int index) const
    {
        Q_UNUSED(index)
        return QString();
    }
    Q_INVOKABLE QString releaseTag(int index) const
    {
        Q_UNUSED(index)
        return QStringLiteral("b12345");
    }
    Q_INVOKABLE QString releaseDate(int index) const
    {
        Q_UNUSED(index)
        return QStringLiteral("2026-11-16");
    }
    Q_INVOKABLE bool releaseIsActive(int index) const
    {
        Q_UNUSED(index)
        return false;
    }
    Q_INVOKABLE void ignoreSelectedVersion() {}
    Q_INVOKABLE void restoreIgnoredVersion() {}
    Q_INVOKABLE void checkForUpdates() {}
    Q_INVOKABLE void startDownloadAndInstall() {}
    Q_INVOKABLE bool activateCandidate() { return m_candidateInstalled; }
    Q_INVOKABLE void cancelInstall() {}
    Q_INVOKABLE QString cleanupUnusedBuilds() { return QString(); }
    Q_INVOKABLE void rescanInstalledBuilds() {}
    Q_INVOKABLE QString activateBuild(int index)
    {
        Q_UNUSED(index)
        return QString();
    }
    Q_INVOKABLE QString openBuildFolder(int index)
    {
        Q_UNUSED(index)
        return QString();
    }
    Q_INVOKABLE QString removeBuild(int index)
    {
        Q_UNUSED(index)
        return QString();
    }
    Q_INVOKABLE QString removeBuild(const QString &tag)
    {
        Q_UNUSED(tag)
        return QString();
    }
    Q_INVOKABLE static QString backendDisplayName(const QString &backend) { return backend; }
    Q_INVOKABLE void openReleasePage() {}

signals:
    void backendChanged();
    void channelChanged();
    void selectedReleaseChanged();
    void catalogChanged();
    void installedChanged();
    void installedBuildsChanged();
    void stateChanged();
    void hasUpdateChanged();
    void candidateChanged();
    void progressChanged();

private:
    FakeQuantModel *m_builds = new FakeQuantModel(this);
    QString m_backend = QStringLiteral("cpu");
    QString m_channel = QStringLiteral("stable");
    QString m_catalogWarning;
    int m_selectedRelease = -1;
    bool m_hasUpdate = false;
    bool m_candidateIgnored = false;
    bool m_candidateInstalled = false;
};

// The stand-in for the runtime controller: the runtime section asks it for the
// memory estimate and the state of the server.
class FakeRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state CONSTANT)
    Q_PROPERTY(int busyState READ busyState CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage CONSTANT)

public:
    enum RuntimeState { NotConfigured = 0, Stopped = 1, Starting = 2, Ready = 3, Stopping = 4, Failed = 5 };
    Q_ENUM(RuntimeState)

    int state() const { return Stopped; }
    int busyState() const { return 0; }
    QString statusMessage() const { return QString(); }

    Q_INVOKABLE QVariantMap estimateModelMemory(const QString &modelPath, const QString &role = QStringLiteral("blockRecognition"))
    {
        Q_UNUSED(modelPath)
        Q_UNUSED(role)
        return {};
    }
    Q_INVOKABLE bool canRecognize(bool documentLoaded) const
    {
        Q_UNUSED(documentLoaded)
        return false;
    }
    Q_INVOKABLE bool taskModelAvailable(const QString &task) const
    {
        Q_UNUSED(task)
        return false;
    }
    Q_INVOKABLE void refreshSingleInstanceLock() {}
    Q_INVOKABLE QString startServer() { return QString(); }
    Q_INVOKABLE void stopServer() {}
    Q_INVOKABLE void restartServer() {}
    Q_INVOKABLE QString launchCommandPreview() { return QString(); }
};

// The stand-in for the launch and request profile stores: the models section
// asks them for the parameter table and writes the values back.
class FakeProfilesStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *draftModel READ draftModel CONSTANT)

public:
    QAbstractItemModel *draftModel() const { return m_model; }

    Q_INVOKABLE void reloadDraft() {}
    Q_INVOKABLE bool setDraftValue(int row, const QString &text)
    {
        Q_UNUSED(row)
        Q_UNUSED(text)
        return true;
    }
    Q_INVOKABLE void saveDraft() {}
    Q_INVOKABLE void loadDefaultDraft() {}
    Q_INVOKABLE bool draftHasSecretLikeParameter() const { return false; }
    Q_INVOKABLE QString modelRuntimeNoteForRole(const QString &task) const
    {
        Q_UNUSED(task)
        return QString();
    }
    Q_INVOKABLE void resetToDefaults() { ++m_resets; }

    void resetResets() { m_resets = 0; }
    int resets() const { return m_resets; }

private:
    FakeQuantModel *m_model = new FakeQuantModel(this);
    int m_resets = 0;
};

}  // namespace

class TestSettingsWindowQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void theWindowHasFiveSections();
    void pickingASectionSwitchesIt();
    void theInterfacePickersKeepTheirSize();
    void theOutputParserKeepsItsOwnWidth();
    void theRuntimeAndModelsBlocksAreCards();
    void theUpdatesCardOffersOneActionAndOneFact();
    void theUpdatesCardActivatesTheBuildThatIsOnDisk();
    void theUpdatesCardShowsTheActiveBuildWhenThereIsNothingToInstall();
    void theCancelRestoresTheInterface();
    void theCancelRestoresTheBlockTypes();
    void theResetWritesAtOnceAndKeepsTheWindowClean();
    void theWizardRequestClosesTheWindowAndAsksForTheWizard();
    void theWizardAsksBeforeItDropsTheChanges();
    void theCloseConfirmDialogSaysWhatItsButtonsDo();
    void theValuesAreReadOncePerSession();
    void theFailedSaveKeepsTheRuntimeModes();

private:
    void registerModuleTypes();
    std::unique_ptr<QObject> loadWindow();
    QVariant evaluate(QObject *scope, const QString &expression);
    QQuickItem *findItemByType(QQuickItem *root, const QString &typeName);

    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<I18n> m_i18n;
    std::unique_ptr<VerificationPromptStore> m_verification;
    std::unique_ptr<ConnectionProbe> m_probe;
    std::unique_ptr<FakeModelInstaller> m_modelInstaller;
    std::unique_ptr<FakeRuntimeInstaller> m_runtimeInstaller;
    std::unique_ptr<FakeRuntime> m_runtime;
    std::unique_ptr<FakeController> m_controller;
    std::unique_ptr<FakeProfilesStore> m_launchProfiles;
    std::vector<std::unique_ptr<FakeProfilesStore>> m_profiles;
    std::unique_ptr<QQmlEngine> m_engine;
    QTemporaryDir m_runtimeDir;
    QStringList m_warnings;
    QString m_qmlRoot;
};

void TestSettingsWindowQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(m_runtimeDir.isValid());

    // The runtime and the models live in the temporary directory: the reset
    // writes into the profiles directory, and the user's disk is not the
    // test's business.
    m_settings = std::make_unique<SettingsStore>();
    m_settings->setRuntimeRootDir(m_runtimeDir.path());
    m_settings->setRuntimeModelsDir(m_runtimeDir.path());

    m_uiController = std::make_unique<UiController>(*m_settings);
    m_i18n = std::make_unique<I18n>(*m_settings);
    m_verification = std::make_unique<VerificationPromptStore>(*m_settings);
    m_probe = std::make_unique<ConnectionProbe>(*m_settings);
    m_modelInstaller = std::make_unique<FakeModelInstaller>();
    m_runtimeInstaller = std::make_unique<FakeRuntimeInstaller>();
    m_runtime = std::make_unique<FakeRuntime>();
    m_controller = std::make_unique<FakeController>();
    m_launchProfiles = std::make_unique<FakeProfilesStore>();

    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", m_settings.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "I18n", m_i18n.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Verification", m_verification.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "ModelInstaller", m_modelInstaller.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RuntimeInstaller", m_runtimeInstaller.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", m_runtime.get());
    // The check of the shared server goes through the runtime layer.
    qmlRegisterSingletonInstance("LLocr", 1, 0, "ConnectionProbe", m_probe.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Controller", m_controller.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "LaunchProfiles", m_launchProfiles.get());
    for (const char *name : {"RequestProfilesBlock", "RequestProfilesDecision", "RequestProfilesLayout", "RequestProfilesFix"}) {
        m_profiles.push_back(std::make_unique<FakeProfilesStore>());
        qmlRegisterSingletonInstance("LLocr", 1, 0, name, m_profiles.back().get());
    }

    // The module's own singletons, read from the source tree.
    const auto registerSingleton = [this](const char *name) { qmlRegisterSingletonType(QUrl::fromLocalFile(m_qmlRoot + QStringLiteral("/%1.qml").arg(name)), "LLocr", 1, 0, name); };
    registerSingleton("BlockNames");
    registerSingleton("PageStatus");
    registerSingleton("RoleProfiles");
    registerSingleton("TaskStatus");
    registerSingleton("Theme");

    // The QML module of the app (qt_add_qml_module) turns every file into a
    // type of the LLocr module. The window is loaded from the source tree
    // here, so the same types are registered by hand — otherwise the
    // components it uses (LLOLabel, SettingsSectionNav, …) would not resolve.
    registerModuleTypes();

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    // Common/*.qml read Theme without importing LLocr (module-wide visibility
    // inside the app); a context property covers them when the window is
    // loaded from disk.
    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Theme"), theme);
}

void TestSettingsWindowQml::registerModuleTypes()
{
    const QDir root(m_qmlRoot);
    const auto registerFile = [](const QFileInfo &info) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QByteArray head = file.read(64);
        const bool singleton = head.contains("pragma Singleton");
        const QUrl url = QUrl::fromLocalFile(info.absoluteFilePath());
        if (singleton)
            qmlRegisterSingletonType(url, "LLocr", 1, 0, info.completeBaseName().toLatin1().constData());
        else
            qmlRegisterType(url, "LLocr", 1, 0, info.completeBaseName().toLatin1().constData());
    };

    const QFileInfoList entries = root.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (info.isDir()) {
            const QDir sub(info.absoluteFilePath());
            const QFileInfoList files = sub.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &file : files) {
                if (file.suffix() == QLatin1String("qml"))
                    registerFile(file);
            }
            continue;
        }
        if (info.suffix() == QLatin1String("qml"))
            registerFile(info);
    }
}

QVariant TestSettingsWindowQml::evaluate(QObject *scope, const QString &expression)
{
    QQmlContext *context = m_engine->contextForObject(scope);
    if (!context)
        return {};
    QQmlExpression expr(context, scope, expression);
    if (expr.hasError()) {
        m_warnings.append(expr.error().toString());
        return {};
    }
    return expr.evaluate();
}

std::unique_ptr<QObject> TestSettingsWindowQml::loadWindow()
{
    m_warnings.clear();

    const QString windowPath = QDir(m_qmlRoot).filePath(QStringLiteral("Settings/SettingsWindow.qml"));
    QQmlComponent window(m_engine.get(), QUrl::fromLocalFile(windowPath));
    if (!window.isReady()) {
        m_warnings.append(window.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(window.create());
    if (!object)
        m_warnings.append(window.errorString());
    return object;
}

// The item of the given QML type in the tree of the root. A scroll view
// keeps its content in the content item, not in the children.
QQuickItem *TestSettingsWindowQml::findItemByType(QQuickItem *root, const QString &typeName)
{
    if (!root)
        return nullptr;
    const QString name = QString::fromLatin1(root->metaObject()->className());
    if (name == typeName || name.startsWith(typeName + QStringLiteral("_QMLTYPE")))
        return root;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findItemByType(child, typeName))
            return found;
    }
    QObject *content = root->property("contentItem").value<QObject *>();
    if (content && content != root)
        return findItemByType(qobject_cast<QQuickItem *>(content), typeName);
    return nullptr;
}

void TestSettingsWindowQml::theWindowHasFiveSections()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    const QVariantList sections = window->property("sections").toList();
    QCOMPARE(sections.size(), 5);

    QStringList ids;
    for (const QVariant &section : sections)
        ids << section.toMap().value(QStringLiteral("id")).toString();
    QCOMPARE(ids, QStringList({QStringLiteral("interface"), QStringLiteral("output"), QStringLiteral("verification"), QStringLiteral("runtime"), QStringLiteral("models")}));
}

void TestSettingsWindowQml::pickingASectionSwitchesIt()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY(QMetaObject::invokeMethod(window.get(), "showSection", Q_ARG(QVariant, QStringLiteral("runtime"))));
    QCoreApplication::processEvents();

    QCOMPARE(evaluate(window.get(), QStringLiteral("nav.currentIndex")).toInt(), 3);
    // The title is the section's own: the catalog may translate it.
    QCOMPARE(evaluate(window.get(), QStringLiteral("nav.currentSectionTitle")).toString(), window->property("sections").toList().at(3).toMap().value(QStringLiteral("title")).toString());
    // Switching the section is not an edit.
    QCOMPARE(window->property("dirty").toBool(), false);
}

// The section gets the whole height of the window; the pickers must keep
// their natural size and sit at the top of it, not be spread over it.
void TestSettingsWindowQml::theInterfacePickersKeepTheirSize()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The section is taller than its content — that is the whole point.
    const double sectionHeight = evaluate(window.get(), QStringLiteral("interfaceSection.height")).toDouble();
    QVERIFY(sectionHeight > 300.0);

    // The language picker (the second child of the layout: the label, the
    // picker, the spacer, the label, the theme switch) and the theme switch
    // keep their own height and sit right under their labels.
    const double languageHeight = evaluate(window.get(), QStringLiteral("interfaceSection.children[0].children[1].height")).toDouble();
    const double languageImplicit = evaluate(window.get(), QStringLiteral("interfaceSection.children[0].children[1].implicitHeight")).toDouble();
    QVERIFY(languageImplicit > 0.0);
    QCOMPARE(languageHeight, languageImplicit);

    const double themeHeight = evaluate(window.get(), QStringLiteral("interfaceSection.children[0].children[4].height")).toDouble();
    const double themeImplicit = evaluate(window.get(), QStringLiteral("interfaceSection.children[0].children[4].implicitHeight")).toDouble();
    QVERIFY(themeImplicit > 0.0);
    QCOMPARE(themeHeight, themeImplicit);

    // The content is packed at the top: the language picker does not float in
    // the middle of the section and the theme switch is not pushed to its
    // bottom by the free height.
    const double languageTop = evaluate(window.get(), QStringLiteral("interfaceSection.children[0].children[1].y")).toDouble();
    QVERIFY2(languageTop < 100.0, qPrintable(QStringLiteral("the language picker is at %1 px").arg(languageTop)));

    const double themeTop = evaluate(window.get(), QStringLiteral("interfaceSection.children[0].children[4].y")).toDouble();
    QVERIFY2(themeTop < 200.0, qPrintable(QStringLiteral("the theme switch is at %1 px").arg(themeTop)));

    // The two pickers share one measure: the language dropdown is as wide as
    // the theme switch.
    const double languageWidth = evaluate(window.get(), QStringLiteral("interfaceSection.children[0].children[1].width")).toDouble();
    const double themeWidth = evaluate(window.get(), QStringLiteral("interfaceSection.children[0].children[4].width")).toDouble();
    QVERIFY(themeWidth > 0.0);
    QCOMPARE(languageWidth, themeWidth);
}

// The parser names are short: the dropdown keeps its own measure instead of
// stretching over the whole section.
void TestSettingsWindowQml::theOutputParserKeepsItsOwnWidth()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY(QMetaObject::invokeMethod(window.get(), "showSection", Q_ARG(QVariant, QStringLiteral("output"))));
    QCoreApplication::processEvents();

    // The parser picker is the second child of the output layout (the label,
    // the picker, the spacer, the checkboxes).
    const double pickerWidth = evaluate(window.get(), QStringLiteral("outputSection.children[0].children[1].width")).toDouble();
    const double sectionWidth = evaluate(window.get(), QStringLiteral("outputSection.width")).toDouble();
    QCOMPARE(pickerWidth, 340.0);
    QVERIFY2(pickerWidth < sectionWidth, qPrintable(QStringLiteral("the picker is %1 px of %2 px").arg(pickerWidth).arg(sectionWidth)));
}

// The runtime management and the downloaded models are cards of their own,
// with the headers the block titles of the verification section carry. The
// runtime section keeps its blocks apart: the builds, the updates and the
// shared server.
void TestSettingsWindowQml::theRuntimeAndModelsBlocksAreCards()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY(QMetaObject::invokeMethod(window.get(), "showSection", Q_ARG(QVariant, QStringLiteral("runtime"))));
    QCoreApplication::processEvents();
    QObject *runtime = evaluate(window.get(), QStringLiteral("runtimeSection")).value<QObject *>();
    QVERIFY(runtime);
    QVERIFY2(hasLabelWithText(runtime, QStringLiteral("llama.cpp builds")), "the builds card has no header");
    QVERIFY2(hasLabelWithText(runtime, QStringLiteral("Updates")), "the updates card has no header");
    QVERIFY2(hasLabelWithText(runtime, QStringLiteral("Shared server")), "the shared server card has no header");
    // The per-task card is gone: the tasks are configured in the table of the
    // models section, and the shared server says so.
    QVERIFY2(!hasLabelWithText(runtime, QStringLiteral("Tasks")), "the tasks card is still there");
    QVERIFY2(hasLabelWithText(runtime, QStringLiteral("Only the external tasks use this address. A different server can be set in the task's settings.")),
             "the shared server card says nothing about the external tasks");
    QVERIFY2(hasLabelWithText(runtime, QStringLiteral("Timeouts")), "the timeouts block has no header");
    QVERIFY2(!hasLabelWithText(runtime, QStringLiteral("Install llama.cpp")), "the old runtime header is still there");
    // The divider between the connection settings and the blocks is gone: the
    // card carries the separation now.
    QObject *runtimeRoot = evaluate(window.get(), QStringLiteral("runtimeSection")).value<QObject *>();
    QVERIFY(runtimeRoot);
    QQuickItem *runtimeTab = findRuntimeTab(qobject_cast<QQuickItem *>(runtimeRoot));
    QVERIFY(runtimeTab);
    QVERIFY2(!hasHairlineChild(runtimeTab), "the runtime section still has a hairline divider");

    // The managed card stands first: the local runtime is the app's own, the
    // shared server is the default of the external tasks.
    int managedCard = -1;
    int sharedCard = -1;
    const QList<QQuickItem *> cards = runtimeTab->childItems();
    for (int i = 0; i < cards.size(); ++i) {
        const QString name = QString::fromLatin1(cards[i]->metaObject()->className());
        if (name.startsWith(QStringLiteral("RuntimeTabInternal")))
            managedCard = i;
        else if (name.startsWith(QStringLiteral("RuntimeTabExternal")))
            sharedCard = i;
    }
    QVERIFY2(managedCard >= 0, "the runtime tab has no managed card");
    QVERIFY2(sharedCard >= 0, "the runtime tab has no shared server card");
    QVERIFY2(managedCard < sharedCard, "the managed card must stand above the shared server card");

    QVERIFY(QMetaObject::invokeMethod(window.get(), "showSection", Q_ARG(QVariant, QStringLiteral("models"))));
    QCoreApplication::processEvents();
    QObject *models = evaluate(window.get(), QStringLiteral("modelsSection")).value<QObject *>();
    QVERIFY(models);
    QVERIFY2(hasLabelWithText(models, QStringLiteral("Downloaded models")), "the models card has no «Downloaded models» header");
    QVERIFY2(!hasLabelWithText(models, QStringLiteral("Downloaded model files on disk")), "the old models header is still there");
}

// The update card with a candidate: one row of facts with a chip, one
// action — the button that starts the installation — and the quiet settings
// under it. The version can be ignored, and the manual choice of a release
// is not a select in the main flow (ADR 315).
void TestSettingsWindowQml::theUpdatesCardOffersOneActionAndOneFact()
{
    m_runtimeInstaller->setHasUpdate(true);
    m_runtimeInstaller->setCandidateIgnored(false);
    m_runtimeInstaller->setCandidateInstalled(false);
    // The card lives in the managed half of the runtime tab.
    m_settings->setConnectionMode(QStringLiteral("managed"));

    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY(QMetaObject::invokeMethod(window.get(), "showSection", Q_ARG(QVariant, QStringLiteral("runtime"))));
    QCoreApplication::processEvents();
    QObject *runtime = evaluate(window.get(), QStringLiteral("runtimeSection")).value<QObject *>();
    QVERIFY(runtime);

    // The check leaves its mark in the header of the card.
    QQuickItem *checked = findItemWithText(runtime, QStringLiteral("Checked at 09:23"));
    QVERIFY2(checked, "the header does not say when the catalog was looked at");
    QVERIFY(checked->isVisible());

    // One fact in one place: the candidate row with its chip.
    QQuickItem *chip = findItemWithText(runtime, QStringLiteral("Available"));
    QVERIFY2(chip, "the candidate row has no chip");
    QVERIFY(chip->isVisible());
    QQuickItem *facts = findItemWithText(runtime, QStringLiteral("b12345 · 2026-11-16 · 649 MiB"));
    QVERIFY2(facts, "the candidate row does not carry the facts of the release");
    QVERIFY(facts->isVisible());

    // One action: the button that starts the installation.
    QQuickItem *update = findItemWithText(runtime, QStringLiteral("Update"));
    QVERIFY2(update, "the card has no «Update» action");
    QVERIFY(update->isVisible());

    // The way out of the offer, and the quiet settings without a select.
    QQuickItem *ignore = findItemWithText(runtime, QStringLiteral("Ignore this version"));
    QVERIFY2(ignore, "the card has no way to ignore the version");
    QVERIFY(ignore->isVisible());
    QVERIFY2(!findItemWithText(runtime, QStringLiteral("Release")), "the quiet settings row still has a «Release» select");

    // The other state of the card is not shown at the same time.
    QQuickItem *active = findItemWithText(runtime, QStringLiteral("Active b12345 — this is the latest build."));
    QVERIFY(active);
    QVERIFY2(!active->isVisible(), "the card shows two states at once");
}

// The candidate that is on disk already: the card offers the switch to it,
// not a download of it (ADR 317).
void TestSettingsWindowQml::theUpdatesCardActivatesTheBuildThatIsOnDisk()
{
    m_runtimeInstaller->setHasUpdate(true);
    m_runtimeInstaller->setCandidateIgnored(false);
    m_runtimeInstaller->setCandidateInstalled(true);
    // The card lives in the managed half of the runtime tab.
    m_settings->setConnectionMode(QStringLiteral("managed"));

    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY(QMetaObject::invokeMethod(window.get(), "showSection", Q_ARG(QVariant, QStringLiteral("runtime"))));
    QCoreApplication::processEvents();
    QObject *runtime = evaluate(window.get(), QStringLiteral("runtimeSection")).value<QObject *>();
    QVERIFY(runtime);

    // The chip names the state: the build is there, it is not offered.
    QQuickItem *chip = findItemWithText(runtime, QStringLiteral("Installed"));
    QVERIFY2(chip, "the candidate row has no chip");
    QVERIFY(chip->isVisible());

    // No download, so the size of the archive is not a fact of the row.
    QQuickItem *facts = findItemWithText(runtime, QStringLiteral("b12345 · 2026-11-16"));
    QVERIFY2(facts, "the candidate row does not carry the facts of the release");
    QVERIFY(facts->isVisible());
    QVERIFY2(!findItemWithText(runtime, QStringLiteral("649 MiB")), "the row still carries the size of a download");

    // One action: the switch to the build that is on disk.
    QQuickItem *activate = findItemWithText(runtime, QStringLiteral("Activate"));
    QVERIFY2(activate, "the card has no «Activate» action");
    QVERIFY(activate->isVisible());
    QVERIFY2(!findItemWithText(runtime, QStringLiteral("Update")), "the card still offers a download of the build that is on disk");
}

// Nothing to install: the card says which build runs and that it is the
// latest one — no action, no candidate.
void TestSettingsWindowQml::theUpdatesCardShowsTheActiveBuildWhenThereIsNothingToInstall()
{
    m_runtimeInstaller->setHasUpdate(false);
    m_runtimeInstaller->setCandidateIgnored(false);
    m_runtimeInstaller->setCandidateInstalled(false);
    // The card lives in the managed half of the runtime tab.
    m_settings->setConnectionMode(QStringLiteral("managed"));

    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY(QMetaObject::invokeMethod(window.get(), "showSection", Q_ARG(QVariant, QStringLiteral("runtime"))));
    QCoreApplication::processEvents();
    QObject *runtime = evaluate(window.get(), QStringLiteral("runtimeSection")).value<QObject *>();
    QVERIFY(runtime);

    QQuickItem *active = findItemWithText(runtime, QStringLiteral("Active b12345 — this is the latest build."));
    QVERIFY2(active, "the card does not name the build that runs");
    QVERIFY(active->isVisible());

    QQuickItem *available = findItemWithText(runtime, QStringLiteral("Available"));
    QVERIFY(available);
    QVERIFY2(!available->isVisible(), "the card offers a version that it does not have");
    QQuickItem *update = findItemWithText(runtime, QStringLiteral("Update"));
    QVERIFY(update);
    QVERIFY2(!update->isVisible(), "the card asks for an update that does not exist");
}

void TestSettingsWindowQml::theCancelRestoresTheInterface()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The theme is applied at once, and the section keeps a snapshot of the
    // state it found.
    QVERIFY(QMetaObject::invokeMethod(window.get(), "armSession"));
    m_uiController->setMode(UiController::Dark);
    QCOMPARE(int(m_uiController->mode()), int(UiController::Dark));

    QVERIFY(QMetaObject::invokeMethod(window.get(), "cancelAll"));
    QCoreApplication::processEvents();

    QCOMPARE(int(m_uiController->mode()), int(UiController::System));
}

void TestSettingsWindowQml::theCancelRestoresTheBlockTypes()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The session starts: the section takes the snapshot of the block types.
    QVERIFY(QMetaObject::invokeMethod(window.get(), "show"));
    QCoreApplication::processEvents();

    auto *model = qobject_cast<VerificationBlocksModel *>(m_verification->blockModel());
    QVERIFY(model);
    QVERIFY(model->totalCount() > 0);
    const int row = 0;
    const bool before = model->enabledAt(row);

    // The user switches the type off in the live model.
    model->setEnabled(row, !before);
    QCOMPARE(model->enabledAt(row), !before);

    // Cancel puts the type back where the session found it.
    QVERIFY(QMetaObject::invokeMethod(window.get(), "cancelAll"));
    QCoreApplication::processEvents();

    QCOMPARE(model->enabledAt(row), before);
}

void TestSettingsWindowQml::theResetWritesAtOnceAndKeepsTheWindowClean()
{
    m_settings->setThemeMode(1);
    m_settings->setLanguage(QStringLiteral("ru"));
    for (auto &store : m_profiles)
        store->resetResets();
    m_launchProfiles->resetResets();

    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY(QMetaObject::invokeMethod(window.get(), "resetAll"));
    QCoreApplication::processEvents();

    // The reset writes at once.
    QCOMPARE(int(m_settings->themeMode()), 0);
    QCOMPARE(m_settings->language(), QStringLiteral("system"));
    // The request profile of every task goes back to the built-in one.
    for (auto &store : m_profiles)
        QCOMPARE(store->resets(), 1);
    QCOMPARE(m_launchProfiles->resets(), 1);
    // The window stays open on the reset values and does not claim unsaved
    // changes.
    QCOMPARE(window->property("dirty").toBool(), false);

    // A Cancel after the reset keeps the reset.
    m_uiController->setMode(UiController::Dark);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "cancelAll"));
    QCoreApplication::processEvents();
    QCOMPARE(int(m_uiController->mode()), int(UiController::System));
}

void TestSettingsWindowQml::theWizardRequestClosesTheWindowAndAsksForTheWizard()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The window has to be on screen: a window that was never shown does not
    // close.
    QVERIFY(QMetaObject::invokeMethod(window.get(), "show"));
    QCoreApplication::processEvents();
    QCOMPARE(window->property("visible").toBool(), true);

    QSignalSpy wizardSpy(window.get(), SIGNAL(wizardRequested()));
    QVERIFY(QMetaObject::invokeMethod(window.get(), "requestWizard"));

    // The signal comes through a callLater: the window must be gone by then.
    QTRY_COMPARE(wizardSpy.count(), 1);
    QCOMPARE(window->property("pendingWizard").toBool(), false);
    QCOMPARE(window->property("visible").toBool(), false);
}

void TestSettingsWindowQml::theWizardAsksBeforeItDropsTheChanges()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY(QMetaObject::invokeMethod(window.get(), "show"));
    QCoreApplication::processEvents();

    // The user has unsaved changes: the wizard may not start over them.
    window->setProperty("dirty", true);
    QSignalSpy wizardSpy(window.get(), SIGNAL(wizardRequested()));
    QVERIFY(QMetaObject::invokeMethod(window.get(), "requestWizard"));
    QCoreApplication::processEvents();

    QCOMPARE(window->property("visible").toBool(), true);
    QCOMPARE(wizardSpy.count(), 0);
    QCOMPARE(window->property("pendingWizard").toBool(), true);

    // The user refuses to drop the changes: the wizard request dies with them.
    QObject *dialog = evaluate(window.get(), QStringLiteral("closeConfirmDialog")).value<QObject *>();
    QVERIFY2(dialog, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
    QCoreApplication::processEvents();
    QCOMPARE(window->property("pendingWizard").toBool(), false);

    // A later close does not start the wizard either.
    window->setProperty("dirty", false);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "close"));
    QCoreApplication::processEvents();
    QCOMPARE(wizardSpy.count(), 0);
}

// The two buttons of the confirmation say what they do (ADR 316): one goes
// back to the editing, the other drops the changes and closes the window.
// "Cancel" would say nothing, and it would collide with the "Cancel" of the
// window's footer, which does the opposite.
void TestSettingsWindowQml::theCloseConfirmDialogSaysWhatItsButtonsDo()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QObject *dialog = evaluate(window.get(), QStringLiteral("closeConfirmDialog")).value<QObject *>();
    QVERIFY2(dialog, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCOMPARE(evaluate(dialog, QStringLiteral("footer.cancelText")).toString(), QStringLiteral("Continue"));
    QCOMPARE(evaluate(dialog, QStringLiteral("footer.acceptText")).toString(), QStringLiteral("Don't save"));
}

void TestSettingsWindowQml::theValuesAreReadOncePerSession()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The window comes up: the session starts and the values are read.
    QVERIFY(QMetaObject::invokeMethod(window.get(), "show"));
    QCoreApplication::processEvents();
    QCOMPARE(window->property("dirty").toBool(), false);
    window->setProperty("dirty", true);

    // The window is hidden and shown again inside the same session: the
    // values are not read a second time, so the drafts and the dirty flag
    // stay where the user left them.
    QVERIFY(QMetaObject::invokeMethod(window.get(), "hide"));
    QCoreApplication::processEvents();
    QVERIFY(QMetaObject::invokeMethod(window.get(), "show"));
    QCoreApplication::processEvents();

    QCOMPARE(window->property("dirty").toBool(), true);
}

// The runtime modes are a plain setting: they are written when the save has
// succeeded. A save that reports a missing model leaves them alone, so the
// user's next Cancel can still take them back.
void TestSettingsWindowQml::theFailedSaveKeepsTheRuntimeModes()
{
    // Every task runs on the local server; the user moves one of them to an
    // external server.
    for (const QString &role : connectionRoleIds())
        m_settings->setRuntimeModeForRole(role, QStringLiteral("managed"));

    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QObject *models = evaluate(window.get(), QStringLiteral("modelsSection")).value<QObject *>();
    QVERIFY(models);

    // The user moves the decision task to an external server.
    QQuickItem *table = findItemByType(qobject_cast<QQuickItem *>(models), QStringLiteral("TaskModelsTable"));
    QVERIFY(table);
    QVERIFY(QMetaObject::invokeMethod(table, "setRuntimeModeForTask", Q_ARG(QVariant, QStringLiteral("decision")), Q_ARG(QVariant, QStringLiteral("external"))));
    QCoreApplication::processEvents();
    QCOMPARE(evaluate(table, QStringLiteral("draftEntry('decision').runtimeMode")).toString(), QStringLiteral("external"));
    // The counter of the section follows the draft: the rows on screen and
    // the numbers above them agree.
    QCOMPARE(evaluate(models, QStringLiteral("managedCount")).toInt(), 3);
    QCOMPARE(evaluate(models, QStringLiteral("externalCount")).toInt(), 1);
    QCOMPARE(labelTextStartingWith(models, QStringLiteral("Local:")), QStringLiteral("Local: 3 · External API: 1"));

    // The model of the task is not on disk: the save fails.
    QVariantMap missing;
    missing.insert(QStringLiteral("task"), QStringLiteral("decision"));
    missing.insert(QStringLiteral("model"), QStringLiteral("minicpm-jev"));
    missing.insert(QStringLiteral("quantId"), QStringLiteral("Q4_K_M"));
    missing.insert(QStringLiteral("size"), 1024);
    m_modelInstaller->setMissingModels({missing});

    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(models, "saveValues", Q_RETURN_ARG(QVariant, result)));
    QCOMPARE(result.toBool(), false);

    // The mode was not written: the window is still open and the user can
    // cancel the switch away.
    QCOMPARE(m_settings->runtimeModeForRole(QStringLiteral("decision")), QStringLiteral("managed"));

    // The save that succeeds writes it.
    m_modelInstaller->setMissingModels({});
    QVERIFY(QMetaObject::invokeMethod(models, "saveValues", Q_RETURN_ARG(QVariant, result)));
    QCOMPARE(result.toBool(), true);
    QCOMPARE(m_settings->runtimeModeForRole(QStringLiteral("decision")), QStringLiteral("external"));

    m_settings->setRuntimeModeForRole(QStringLiteral("decision"), QStringLiteral("managed"));
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_settings_window_qml"));
    TestSettingsWindowQml test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_settings_window_qml.moc"
