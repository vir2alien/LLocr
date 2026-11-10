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
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtTest>

#include "app/I18n.h"
#include "app/UiController.h"
#include "app/VerificationPromptStore.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

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
    Q_INVOKABLE QVariantList applyTaskChoices(const QVariantList &choices) { return choices; }
    Q_INVOKABLE void queueTaskInstalls(const QVariantList &choices) { Q_UNUSED(choices) }
    Q_INVOKABLE QVariantList defaultTaskChoices(qint64 budgetBytes = -1)
    {
        Q_UNUSED(budgetBytes)
        return {};
    }
    Q_INVOKABLE void cancelTaskInstalls() {}

signals:
    void installedChanged();
    void presetsChanged();

private:
    FakeQuantModel *m_models = new FakeQuantModel(this);
};

// The stand-in for the runtime installer: the runtime section shows the
// backend, the release and the installed builds.
class FakeRuntimeInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state CONSTANT)
    Q_PROPERTY(bool busy READ busy CONSTANT)
    Q_PROPERTY(QString platformLabel READ platformLabel CONSTANT)
    Q_PROPERTY(QString recommendedBackend READ recommendedBackend CONSTANT)
    Q_PROPERTY(QString recommendationReason READ recommendationReason CONSTANT)
    Q_PROPERTY(QStringList availableBackends READ availableBackends CONSTANT)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(int releaseCount READ releaseCount CONSTANT)
    Q_PROPERTY(int selectedRelease READ selectedRelease WRITE setSelectedRelease NOTIFY selectedReleaseChanged)
    Q_PROPERTY(QString installedBuild READ installedBuild CONSTANT)
    Q_PROPERTY(QString installedBackend READ installedBackend CONSTANT)
    Q_PROPERTY(bool hasUpdate READ hasUpdate CONSTANT)
    Q_PROPERTY(QString updateBuild READ updateBuild CONSTANT)
    Q_PROPERTY(QString updateTimestampLabel READ updateTimestampLabel CONSTANT)
    Q_PROPERTY(int installedBuildCount READ installedBuildCount CONSTANT)
    Q_PROPERTY(QObject *installedBuilds READ installedBuilds CONSTANT)
    Q_PROPERTY(double progress READ progress CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage CONSTANT)
    Q_PROPERTY(bool canInstall READ canInstall CONSTANT)

public:
    enum State { Idle = 0, Fetching = 1, Ready = 2, Downloading = 3, Installing = 4, Installed = 5, Error = 6 };
    Q_ENUM(State)

    int state() const { return Idle; }
    bool busy() const { return false; }
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
    int releaseCount() const { return 0; }
    int selectedRelease() const { return m_selectedRelease; }
    void setSelectedRelease(int index)
    {
        if (m_selectedRelease == index)
            return;
        m_selectedRelease = index;
        emit selectedReleaseChanged();
    }
    QString installedBuild() const { return QString(); }
    QString installedBackend() const { return QString(); }
    bool hasUpdate() const { return false; }
    QString updateBuild() const { return QString(); }
    QString updateTimestampLabel() const { return QString(); }
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
    Q_INVOKABLE void checkForUpdates() {}
    Q_INVOKABLE void startDownloadAndInstall() {}
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
    Q_INVOKABLE void installUpdate() {}

signals:
    void backendChanged();
    void selectedReleaseChanged();
    void catalogChanged();
    void installedChanged();
    void installedBuildsChanged();
    void stateChanged();

private:
    FakeQuantModel *m_builds = new FakeQuantModel(this);
    QString m_backend = QStringLiteral("cpu");
    int m_selectedRelease = -1;
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
    void theCancelRestoresTheInterface();
    void theResetWritesAtOnceAndKeepsTheWindowClean();
    void theWizardRequestClosesTheWindowAndAsksForTheWizard();
    void theWizardAsksBeforeItDropsTheChanges();
    void theValuesAreReadOncePerSession();

private:
    void registerModuleTypes();
    std::unique_ptr<QObject> loadWindow();
    QVariant evaluate(QObject *scope, const QString &expression);

    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<I18n> m_i18n;
    std::unique_ptr<VerificationPromptStore> m_verification;
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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_settings_window_qml"));
    TestSettingsWindowQml test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_settings_window_qml.moc"
