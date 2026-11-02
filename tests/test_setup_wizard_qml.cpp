// The setup wizard's steps: the check step must not report a failure where
// the setup is fine, and the hardware step must really pick the quantization
// that fits the memory.
//
// Three defects made the wizard's last step show "Something failed" on a
// healthy setup:
//
//  * the self-test signals "finished" when it starts, and the step read that
//    notification as a verdict — the red "Something failed" appeared while
//    the check was still running;
//  * a verdict of an earlier visit of the wizard opened the Finish gate;
//  * the hardware step's auto-selection never ran: the filter that guarded
//    it asked for a request profile id, and every task has one by default,
//    so the biggest quantization was taken instead of the one that fits the
//    memory.
//
// The test loads the shipped QML and watches the steps' own state.

#include <QAbstractListModel>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>

#include "app/I18n.h"
#include "app/UiController.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// The fake models list: one row per profile, with the quantizations the test
// needs. The table asks it for a row and for the row's index.
class FakeQuantModel : public QAbstractListModel
{
    Q_OBJECT

public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : m_rows.size(); }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
            return {};
        const QVariantMap &row = m_rows.at(index.row());
        return role == Qt::UserRole + 1 ? row.value(QStringLiteral("key")) : row.value(QStringLiteral("title"));
    }

    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, QByteArrayLiteral("key")}, {Qt::UserRole + 2, QByteArrayLiteral("title")}}; }

    Q_INVOKABLE QVariantMap rowFor(const QString &key) const
    {
        for (const QVariantMap &row : m_rows) {
            if (row.value(QStringLiteral("key")).toString() == key)
                return row;
        }
        return {};
    }

    Q_INVOKABLE int rowIndexOf(const QString &key) const
    {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).value(QStringLiteral("key")).toString() == key)
                return i;
        }
        return -1;
    }

    void addRow(const QString &key, const QString &title, const QStringList &quantIds)
    {
        QVariantList quants;
        for (const QString &id : quantIds) {
            quants.append(QVariantMap{{QStringLiteral("id"), id},
                                      {QStringLiteral("sizeLabel"), QStringLiteral("1.0 GiB")},
                                      {QStringLiteral("vramLabel"), QString()},
                                      {QStringLiteral("installed"), false},
                                      {QStringLiteral("downloadable"), true}});
        }
        m_rows.append(QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("title"), title}, {QStringLiteral("quants"), quants}, {QStringLiteral("selected"), -1}});
    }

    explicit FakeQuantModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

private:
    QList<QVariantMap> m_rows;
};

// A stand-in for the ModelInstaller singleton: the wizard's steps ask it for
// the models list, for the auto-selection and for the install queue.
class FakeModelInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *allModels READ allModels CONSTANT)
    Q_PROPERTY(QObject *checkQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *decisionQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *layoutQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *fixQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(bool busy READ busy CONSTANT)
    Q_PROPERTY(int state READ state CONSTANT)
    Q_PROPERTY(bool taskQueueActive READ taskQueueActive CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage CONSTANT)

public:
    enum State { Idle = 0, Ready, Installing, Error };
    Q_ENUM(State)

    QObject *allModels() const { return m_models; }
    QObject *quantModels() const { return m_models; }
    bool busy() const { return false; }
    int state() const { return Idle; }
    bool taskQueueActive() const { return false; }
    QString statusMessage() const { return {}; }

    Q_INVOKABLE QVariantList defaultTaskChoices() const { return m_defaultChoices; }
    Q_INVOKABLE QVariantList applyTaskChoices(const QVariantList &choices)
    {
        Q_UNUSED(choices)
        return {};
    }
    Q_INVOKABLE void refreshInstalled() {}
    Q_INVOKABLE void reloadPresets() {}
    Q_INVOKABLE void rescanRegistry() {}
    Q_INVOKABLE void queueTaskInstalls(const QVariantList &choices) { Q_UNUSED(choices) }

    void setDefaultChoices(const QVariantList &choices) { m_defaultChoices = choices; }
    void addRow(const QString &key, const QString &title, const QStringList &quantIds) { m_models->addRow(key, title, quantIds); }

signals:
    void installedChanged();
    void presetsChanged();
    void taskQueueStateChanged();

private:
    FakeQuantModel *m_models = new FakeQuantModel(this);
    QVariantList m_defaultChoices;
};

// A stand-in for the RuntimeInstaller singleton: the check step drives the
// download through it.
class FakeRuntimeInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(QString backend READ backend NOTIFY backendChanged)
    Q_PROPERTY(QStringList availableBackends READ availableBackends CONSTANT)
    Q_PROPERTY(double progress READ progress CONSTANT)
    Q_PROPERTY(QString recommendationReason READ recommendationReason CONSTANT)
    Q_PROPERTY(QString platformLabel READ platformLabel CONSTANT)

public:
    enum State { Idle = 0, Fetching, Ready, Downloading, Installing, Installed, Error };
    Q_ENUM(State)

    int state() const { return m_state; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    QString backend() const { return m_backend; }
    QStringList availableBackends() const { return {QStringLiteral("cpu"), QStringLiteral("metal")}; }
    double progress() const { return 0.0; }
    QString recommendationReason() const { return {}; }
    QString platformLabel() const { return {}; }

    Q_INVOKABLE QString backendDisplayName(const QString &backend) const { return backend; }
    Q_INVOKABLE void rescanInstalledBuilds() {}
    Q_INVOKABLE void startDownloadAndInstall() {}
    Q_INVOKABLE void cancelInstall() {}

signals:
    void stateChanged();
    void backendChanged();

private:
    int m_state = Idle;
    bool m_busy = false;
    QString m_statusMessage;
    QString m_backend = QStringLiteral("cpu");
};

// A stand-in for the Runtime singleton: the check step reads the config's
// validity and the server path.
class FakeRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configValid READ configValid CONSTANT)
    Q_PROPERTY(bool serverPathValid READ serverPathValid CONSTANT)

public:
    bool configValid() const { return m_configValid; }
    bool serverPathValid() const { return m_serverPathValid; }
    void setConfigValid(bool valid) { m_configValid = valid; }

private:
    bool m_configValid = true;
    bool m_serverPathValid = true;
};

// A stand-in for the SelfTest singleton. The real one signals "finished"
// when the run starts — the notification of the properties — and again when
// the verdict is there; the fake keeps both.
class FakeSelfTest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool selftestRunning READ selftestRunning NOTIFY selftestFinished)
    Q_PROPERTY(bool selftestOk READ selftestOk NOTIFY selftestFinished)
    Q_PROPERTY(QString selftestMessage READ selftestMessage NOTIFY selftestFinished)

public:
    bool selftestRunning() const { return m_running; }
    bool selftestOk() const { return m_ok; }
    QString selftestMessage() const { return m_message; }

    Q_INVOKABLE void runSelfTestQml()
    {
        m_running = true;
        m_ok = false;
        m_message = QStringLiteral("Running self-test…");
        emit selftestFinished();
    }

    Q_INVOKABLE void resetSelfTest()
    {
        m_ok = false;
        m_message.clear();
        emit selftestFinished();
    }

    // The end of the run.
    void finish(bool ok, const QString &message)
    {
        m_running = false;
        m_ok = ok;
        m_message = message;
        emit selftestFinished();
    }

    void setVerdict(bool ok, const QString &message)
    {
        m_ok = ok;
        m_message = message;
        emit selftestFinished();
    }

signals:
    void selftestFinished();

private:
    bool m_running = false;
    bool m_ok = false;
    QString m_message;
};

// A stand-in for the HardwareInfo singleton: the hardware step shows the GPU
// and the memory.
class FakeHardwareInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString gpuName READ gpuName CONSTANT)
    Q_PROPERTY(QString memoryLabel READ memoryLabel CONSTANT)

public:
    QString gpuName() const { return {}; }
    QString memoryLabel() const { return QStringLiteral("16 GB"); }
};

// A stand-in for the launch/request profile stores: the gear popup behind the
// task table asks them for the parameter table and writes the values back.
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
    Q_INVOKABLE QString modelRuntimeNoteForRole(const QString &task) const
    {
        Q_UNUSED(task)
        return QString();
    }

private:
    QAbstractItemModel *m_model = nullptr;
};

}  // namespace

class TestSetupWizardQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void theCheckStepDoesNotFailWhileTheSelfTestRuns();
    void theFinishGateNeedsACheckOfThisVisit();
    void theHardwareStepPicksTheQuantThatFitsTheMemory();
    void aConfiguredTaskKeepsItsOwnPick();
    void theBackButtonStaysOnTheLastStep();

private:
    void registerModuleTypes();
    std::unique_ptr<QObject> loadFile(const QString &relativePath);
    QVariant evaluate(QObject *scope, const QString &expression);
    QObject *findByTypeName(QObject *root, const QString &typeName);
    QString typeNamesUnder(QObject *root);

    std::unique_ptr<FakeModelInstaller> m_modelInstaller;
    std::unique_ptr<FakeRuntimeInstaller> m_runtimeInstaller;
    std::unique_ptr<FakeRuntime> m_runtime;
    std::unique_ptr<FakeSelfTest> m_selfTest;
    std::unique_ptr<FakeHardwareInfo> m_hardwareInfo;
    std::unique_ptr<FakeProfilesStore> m_profiles;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<I18n> m_i18n;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_parent = nullptr;
    QStringList m_warnings;
    QString m_qmlRoot;
};

void TestSetupWizardQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));

    m_modelInstaller = std::make_unique<FakeModelInstaller>();
    m_runtimeInstaller = std::make_unique<FakeRuntimeInstaller>();
    m_runtime = std::make_unique<FakeRuntime>();
    m_selfTest = std::make_unique<FakeSelfTest>();
    m_hardwareInfo = std::make_unique<FakeHardwareInfo>();
    m_settings = std::make_unique<SettingsStore>();
    m_uiController = std::make_unique<UiController>(*m_settings);
    m_i18n = std::make_unique<I18n>(*m_settings);

    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", m_settings.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "ModelInstaller", m_modelInstaller.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RuntimeInstaller", m_runtimeInstaller.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", m_runtime.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "SelfTest", m_selfTest.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "HardwareInfo", m_hardwareInfo.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "I18n", m_i18n.get());

    // The profile stores are not part of this test: one empty stand-in answers
    // for all of them.
    m_profiles = std::make_unique<FakeProfilesStore>();
    for (const char *name : {"LaunchProfiles", "RequestProfilesBlock", "RequestProfilesDecision", "RequestProfilesLayout", "RequestProfilesFix"})
        qmlRegisterSingletonInstance("LLocr", 1, 0, name, m_profiles.get());

    // The module's own singletons, read from the source tree.
    const auto registerSingleton = [this](const char *name) { qmlRegisterSingletonType(QUrl::fromLocalFile(m_qmlRoot + QStringLiteral("/%1.qml").arg(name)), "LLocr", 1, 0, name); };
    registerSingleton("BlockNames");
    registerSingleton("PageStatus");
    registerSingleton("RoleProfiles");
    registerSingleton("TaskStatus");
    registerSingleton("Theme");

    registerModuleTypes();

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    // The steps are loaded from the source tree, so the Common components they
    // use read Theme without importing LLocr: a context property covers them.
    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Theme"), theme);

    // A window with an item in it: the steps anchor to their parent, and a
    // dialog needs an overlay to live in.
    m_window = std::make_unique<QQuickWindow>();
    m_parent = new QQuickItem(m_window->contentItem());
    m_parent->setSize(QSizeF(800, 600));
    m_window->resize(800, 600);
    m_window->show();
}

void TestSetupWizardQml::registerModuleTypes()
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

std::unique_ptr<QObject> TestSetupWizardQml::loadFile(const QString &relativePath)
{
    m_warnings.clear();

    const QString path = QDir(m_qmlRoot).filePath(relativePath);
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    if (!component.isReady()) {
        m_warnings.append(component.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        m_warnings.append(component.errorString());
        return nullptr;
    }
    if (auto *item = qobject_cast<QQuickItem *>(object.get()))
        item->setParentItem(m_parent);
    return object;
}

QVariant TestSetupWizardQml::evaluate(QObject *scope, const QString &expression)
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

QObject *TestSetupWizardQml::findByTypeName(QObject *root, const QString &typeName)
{
    // A QML type's class name carries the engine's suffix ("TaskModelsTable_QMLTYPE_23").
    const auto matches = [&typeName](const QString &className) { return className == typeName || className.startsWith(typeName + QStringLiteral("_QMLTYPE")); };

    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        if (matches(QString::fromLatin1(child->metaObject()->className())))
            return child;
        if (QObject *found = findByTypeName(child, typeName))
            return found;
    }
    return nullptr;
}

// The names of the types under a root, for a failing lookup's message.
QString TestSetupWizardQml::typeNamesUnder(QObject *root)
{
    QStringList names;
    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        names << child->metaObject()->className();
        names << typeNamesUnder(child);
    }
    return names.join(QStringLiteral(", "));
}

// The check step reads the self-test's notification: the signal comes when
// the run starts and again when the verdict is there. The step must not call
// the start a verdict — the red "Something failed" must not appear while the
// check is still running.
void TestSetupWizardQml::theCheckStepDoesNotFailWhileTheSelfTestRuns()
{
    const std::unique_ptr<QObject> step = loadFile(QStringLiteral("Setup/StepCheck.qml"));
    QVERIFY2(step, qPrintable(m_warnings.join(QStringLiteral("; "))));

    evaluate(step.get(), QStringLiteral("runCheck()"));
    QCoreApplication::processEvents();

    QCOMPARE(evaluate(step.get(), QStringLiteral("phase")).toString(), QStringLiteral("check"));

    // The run ends with a failure: now the step may say so.
    m_selfTest->finish(false, QStringLiteral("the model did not answer"));
    QCoreApplication::processEvents();
    QCOMPARE(evaluate(step.get(), QStringLiteral("phase")).toString(), QStringLiteral("failed"));
}

// The Finish opens on the check of this visit. A verdict of an earlier visit
// is not this visit's verdict.
void TestSetupWizardQml::theFinishGateNeedsACheckOfThisVisit()
{
    const std::unique_ptr<QObject> step = loadFile(QStringLiteral("Setup/StepCheck.qml"));
    QVERIFY2(step, qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The verdict of an earlier visit is still in the singleton.
    m_selfTest->setVerdict(true, QStringLiteral("Self-test passed"));
    QCoreApplication::processEvents();
    QCOMPARE(evaluate(step.get(), QStringLiteral("complete")).toBool(), false);

    // The check of this visit: the gate opens.
    evaluate(step.get(), QStringLiteral("runCheck()"));
    QCoreApplication::processEvents();
    m_selfTest->finish(true, QStringLiteral("the model answered"));
    QCoreApplication::processEvents();
    QCOMPARE(evaluate(step.get(), QStringLiteral("phase")).toString(), QStringLiteral("done"));
    QCOMPARE(evaluate(step.get(), QStringLiteral("complete")).toBool(), true);

    // The wizard opens again: the verdict of the last visit is thrown away.
    m_selfTest->resetSelfTest();
    QCoreApplication::processEvents();
    QCOMPARE(m_selfTest->selftestOk(), false);
    QCOMPARE(m_selfTest->selftestMessage(), QString());
}

// The wizard promises a quantization that fits the memory: the auto-selection
// must reach the table. It used to be filtered away by a request profile id —
// a setting every task has by default.
void TestSetupWizardQml::theHardwareStepPicksTheQuantThatFitsTheMemory()
{
    m_modelInstaller->addRow(QStringLiteral("mineru"), QStringLiteral("MinerU"), {QStringLiteral("q8_0"), QStringLiteral("q4_k_m")});
    m_modelInstaller->setDefaultChoices(QVariantList{
        QVariantMap{{QStringLiteral("task"), QStringLiteral("blockRecognition")}, {QStringLiteral("key"), QStringLiteral("mineru")}, {QStringLiteral("quantId"), QStringLiteral("q4_k_m")}}});

    const std::unique_ptr<QObject> step = loadFile(QStringLiteral("Setup/StepHardware.qml"));
    QVERIFY2(step, qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The step fills itself when it becomes visible.
    step->setProperty("visible", false);
    step->setProperty("visible", true);
    QCoreApplication::processEvents();

    QObject *table = findByTypeName(step.get(), QStringLiteral("TaskModelsTable"));
    QVERIFY2(table, qPrintable(QStringLiteral("the hardware step has no task table: %1").arg(typeNamesUnder(step.get()))));

    const QVariant entry = evaluate(table, QStringLiteral("draftEntry('blockRecognition')"));
    QVERIFY(entry.isValid());
    QCOMPARE(entry.value<QObject *>()->property("quantId").toString(), QStringLiteral("q4_k_m"));
}

// A task the user configured keeps its own pick: the auto-selection belongs
// to the tasks that have no model yet.
void TestSetupWizardQml::aConfiguredTaskKeepsItsOwnPick()
{
    m_modelInstaller->addRow(QStringLiteral("mineru"), QStringLiteral("MinerU"), {QStringLiteral("q8_0"), QStringLiteral("q4_k_m")});
    m_modelInstaller->setDefaultChoices(QVariantList{
        QVariantMap{{QStringLiteral("task"), QStringLiteral("blockRecognition")}, {QStringLiteral("key"), QStringLiteral("mineru")}, {QStringLiteral("quantId"), QStringLiteral("q4_k_m")}}});
    m_settings->setLaunchModelPathForRole(QStringLiteral("blockRecognition"), QStringLiteral("/models/mineru.gguf"));

    const std::unique_ptr<QObject> step = loadFile(QStringLiteral("Setup/StepHardware.qml"));
    QVERIFY2(step, qPrintable(m_warnings.join(QStringLiteral("; "))));

    step->setProperty("visible", false);
    step->setProperty("visible", true);
    QCoreApplication::processEvents();

    QObject *table = findByTypeName(step.get(), QStringLiteral("TaskModelsTable"));
    QVERIFY2(table, qPrintable(QStringLiteral("the hardware step has no task table: %1").arg(typeNamesUnder(step.get()))));

    const QVariant entry = evaluate(table, QStringLiteral("draftEntry('blockRecognition')"));
    QVERIFY(entry.isValid());
    QCOMPARE(entry.value<QObject *>()->property("quantId").toString(), QStringLiteral("q8_0"));

    m_settings->setLaunchModelPathForRole(QStringLiteral("blockRecognition"), QString());
}

// A failed check is fixed by going back to the step that holds the cause:
// the way back stays on the last step.
void TestSetupWizardQml::theBackButtonStaysOnTheLastStep()
{
    const std::unique_ptr<QObject> wizard = loadFile(QStringLiteral("SetupWizard.qml"));
    QVERIFY2(wizard, qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The first step has no way back.
    wizard->setProperty("current", 0);
    QCoreApplication::processEvents();
    QCOMPARE(wizard->property("canGoBack").toBool(), false);

    // The last step has one.
    const int last = wizard->property("lastStep").toInt();
    QVERIFY2(last > 0, "the wizard's step list is empty");
    wizard->setProperty("current", last);
    QCoreApplication::processEvents();
    QCOMPARE(wizard->property("canGoBack").toBool(), true);
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_setup_wizard_qml"));
    TestSetupWizardQml test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_setup_wizard_qml.moc"
