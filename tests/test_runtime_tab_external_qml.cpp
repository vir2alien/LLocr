// The external server tab: the six task rows and what they write.
//
// The tab is the one place where an external connection is configured: the
// shared server plus a row per task (ADR 226). The rows edit the settings
// through the C++ side, and the test checks the round trip — the values the
// user types reach `SettingsStore`, and the values it holds come back into the
// fields.

#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QtTest>

#include "config/SecretStore.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A stand-in for the UiController: Theme reads the dark flag from it.
class FakeUiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark CONSTANT)

public:
    bool dark() const { return false; }
};

}  // namespace

class TestRuntimeTabExternalQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void theTabLoadsWithoutWarnings();
    void theFourTasksHaveARow();
    void theCompactModeHidesTheTasks();
    void theTaskEndpointAndAliasAreSaved();
    void theSavedValuesComeBackIntoTheFields();
    void theHttpRemoteEndpointWarnsAboutPlaintextKey();
    void theTaskRowWarnsAboutPlaintextKeyOnItsOwnEndpoint();
    void theStoredKeyIsShownAsAMaskAndNeverAsText();
    void clearingTheFieldRemovesTheStoredKey();
    void aTypedKeyReplacesTheStoredOne();
    void theHeaderSwitchRevealsEveryKey();
    void theMaskIsNeverMixedWithTheTypedKey();

private:
    void registerModuleTypes();
    std::unique_ptr<QObject> loadTab();
    QVariant evaluate(QObject *scope, const QString &expression);
    QObject *rowAt(QObject *tab, int index);
    QObject *rowForTask(QObject *tab, const QString &task);
    QObject *sharedKeyField(QObject *tab);

    std::unique_ptr<FakeUiController> m_uiController;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<QQmlEngine> m_engine;
    QStringList m_warnings;
    QString m_qmlRoot;
};

void TestRuntimeTabExternalQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));

    m_uiController = std::make_unique<FakeUiController>();
    m_settings = std::make_unique<SettingsStore>();

    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", m_settings.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

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

    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Theme"), theme);

    if (!SecretStore::isAvailable())
        QSKIP("No OS secret store on this machine");
    const QString probe = QStringLiteral("org.llocr.test/probe");
    if (!SecretStore::write(probe, QStringLiteral("probe")))
        QSKIP(qPrintable(QStringLiteral("The OS secret store is not writable here: %1").arg(SecretStore::lastError())));
    SecretStore::remove(probe);
    SecretStore::forgetAllCached();
}

void TestRuntimeTabExternalQml::cleanupTestCase()
{
    m_settingsIsolation.clearSecrets();
}

void TestRuntimeTabExternalQml::registerModuleTypes()
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

QVariant TestRuntimeTabExternalQml::evaluate(QObject *scope, const QString &expression)
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

std::unique_ptr<QObject> TestRuntimeTabExternalQml::loadTab()
{
    m_warnings.clear();

    const QString tabPath = QDir(m_qmlRoot).filePath(QStringLiteral("Settings/RuntimeTabExternal.qml"));
    QQmlComponent tab(m_engine.get(), QUrl::fromLocalFile(tabPath));
    if (!tab.isReady()) {
        m_warnings.append(tab.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(tab.create());
    if (!object)
        m_warnings.append(tab.errorString());
    return object;
}

QObject *TestRuntimeTabExternalQml::rowAt(QObject *tab, int index)
{
    return evaluate(tab, QStringLiteral("taskRows.itemAt(%1)").arg(index)).value<QObject *>();
}

// The rows follow the C++ task list, so a test looks a row up by its task
// rather than by its position.
QObject *TestRuntimeTabExternalQml::rowForTask(QObject *tab, const QString &task)
{
    const int count = evaluate(tab, QStringLiteral("taskRows.count")).toInt();
    for (int i = 0; i < count; ++i) {
        QObject *row = rowAt(tab, i);
        if (row && row->property("task").toString() == task)
            return row;
    }
    return nullptr;
}

void TestRuntimeTabExternalQml::theTabLoadsWithoutWarnings()
{
    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));
}

void TestRuntimeTabExternalQml::theFourTasksHaveARow()
{
    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    // The rows are the tasks the C++ lists (ADR 215): the tab keeps no copy
    // of the list.
    const QVariantList ids = evaluate(tab.get(), QStringLiteral("taskIds")).toList();
    QCOMPARE(ids.size(), 4);
    QCOMPARE(evaluate(tab.get(), QStringLiteral("taskRows.count")).toInt(), 4);

    for (int i = 0; i < ids.size(); ++i) {
        QObject *row = rowAt(tab.get(), i);
        QVERIFY2(row, qPrintable(ids.at(i).toString()));
        QCOMPARE(row->property("task").toString(), ids.at(i).toString());
    }
}

void TestRuntimeTabExternalQml::theCompactModeHidesTheTasks()
{
    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    // The wizard's external step shows the shared server only.
    tab->setProperty("compact", true);
    QCoreApplication::processEvents();
    QCOMPARE(evaluate(tab.get(), QStringLiteral("taskRows.count")).toInt(), 0);
}

void TestRuntimeTabExternalQml::theTaskEndpointAndAliasAreSaved()
{
    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QObject *row = rowForTask(tab.get(), QStringLiteral("blockRecognition"));
    QVERIFY(row);
    QCOMPARE(row->property("task").toString(), QStringLiteral("blockRecognition"));
    row->setProperty("endpointText", QStringLiteral("http://own:9000"));
    row->setProperty("aliasText", QStringLiteral("own-model"));

    evaluate(tab.get(), QStringLiteral("saveValues()"));
    QCoreApplication::processEvents();

    QCOMPARE(m_settings->baseUrlForRole(QStringLiteral("blockRecognition")), QStringLiteral("http://own:9000"));
    QCOMPARE(m_settings->modelNameForRole(QStringLiteral("blockRecognition")), QStringLiteral("own-model"));
}

void TestRuntimeTabExternalQml::theSavedValuesComeBackIntoTheFields()
{
    m_settings->setBaseUrlForRole(QStringLiteral("decision"), QStringLiteral("http://decision:9000"));
    m_settings->setModelNameForRole(QStringLiteral("decision"), QStringLiteral("decision-model"));

    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    evaluate(tab.get(), QStringLiteral("loadValues()"));
    QCoreApplication::processEvents();

    QObject *row = rowForTask(tab.get(), QStringLiteral("decision"));
    QVERIFY(row);
    QCOMPARE(row->property("task").toString(), QStringLiteral("decision"));
    QCOMPARE(row->property("endpointText").toString(), QStringLiteral("http://decision:9000"));
    QCOMPARE(row->property("aliasText").toString(), QStringLiteral("decision-model"));
}

// An http endpoint outside the loopback hosts would carry the key as clear
// text: the warning must name it while the user types, and stay quiet for
// https and the loopback names.
void TestRuntimeTabExternalQml::theHttpRemoteEndpointWarnsAboutPlaintextKey()
{
    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    const QList<QPair<QString, bool>> cases = {
        {QStringLiteral(""), false},
        {QStringLiteral("http://localhost:8080"), false},
        {QStringLiteral("http://127.0.0.1:8080"), false},
        {QStringLiteral("http://[::1]:8080"), false},
        {QStringLiteral("HTTPS://LOCALHOST:8080"), false},
        {QStringLiteral("https://api.example.com/v1"), false},
        {QStringLiteral("example.com:8080"), false},
        {QStringLiteral("http://192.168.1.5:8080"), true},
        {QStringLiteral("http://user:pass@10.0.0.9"), true},
        {QStringLiteral("HTTP://EXAMPLE.ORG:8080"), true},
    };
    for (const auto &endpoint : cases) {
        evaluate(tab.get(), QStringLiteral("baseUrlField.text = %1").arg(endpoint.first.isEmpty() ? QStringLiteral("''") : QStringLiteral("'%1'").arg(endpoint.first)));
        QCoreApplication::processEvents();
        QCOMPARE(evaluate(tab.get(), QStringLiteral("plaintextKeyWarning.visible")).toBool(), endpoint.second);
    }
}

// The per-task row carries the same warning over its own endpoint (ADR 226):
// its field decides, not the shared one, and the shared field stays quiet
// while only the row's endpoint would leak the key.
void TestRuntimeTabExternalQml::theTaskRowWarnsAboutPlaintextKeyOnItsOwnEndpoint()
{
    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    // A remote http shared endpoint lights the tab's warning; the row with an
    // empty endpoint (the shared value) does not speak for it.
    evaluate(tab.get(), QStringLiteral("baseUrlField.text = 'http://10.0.0.9:8080'"));
    QCoreApplication::processEvents();
    QCOMPARE(evaluate(tab.get(), QStringLiteral("plaintextKeyWarning.visible")).toBool(), true);

    QObject *row = rowForTask(tab.get(), QStringLiteral("fix"));
    QVERIFY(row);
    QCOMPARE(row->property("plaintextKeyWarningVisible").toBool(), false);

    const QList<QPair<QString, bool>> cases = {
        {QStringLiteral(""), false},
        {QStringLiteral("http://localhost:8080"), false},
        {QStringLiteral("http://127.0.0.1:8080"), false},
        {QStringLiteral("http://[::1]:8080"), false},
        {QStringLiteral("HTTPS://LOCALHOST:8080"), false},
        {QStringLiteral("https://api.example.com/v1"), false},
        {QStringLiteral("example.com:8080"), false},
        {QStringLiteral("http://192.168.1.5:8080"), true},
        {QStringLiteral("http://user:pass@10.0.0.9"), true},
        {QStringLiteral("HTTP://EXAMPLE.ORG:8080"), true},
    };
    for (const auto &endpoint : cases) {
        row->setProperty("endpointText", endpoint.first);
        QCoreApplication::processEvents();
        QCOMPARE(row->property("plaintextKeyWarningVisible").toBool(), endpoint.second);
    }
}

// The shared key field of the tab.
QObject *TestRuntimeTabExternalQml::sharedKeyField(QObject *tab)
{
    return evaluate(tab, QStringLiteral("sharedKey")).value<QObject *>();
}

// A stored key is never shown back (ADR 227): the field carries the mask
// and takes a new key.
void TestRuntimeTabExternalQml::theStoredKeyIsShownAsAMaskAndNeverAsText()
{
    m_settings->setApiKey(QStringLiteral("super-secret"));

    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    evaluate(tab.get(), QStringLiteral("loadValues()"));
    QCoreApplication::processEvents();

    QObject *shared = sharedKeyField(tab.get());
    QVERIFY(shared);
    QCOMPARE(shared->property("keySet").toBool(), true);

    const QString mask = shared->property("maskText").toString();
    QVERIFY(!mask.isEmpty());
    QCOMPARE(shared->property("text").toString(), mask);
    QVERIFY(shared->property("text").toString() != QStringLiteral("super-secret"));
}

// The field is the way to remove a key: an erased mask is an empty field,
// and an empty field removes the stored key.
void TestRuntimeTabExternalQml::clearingTheFieldRemovesTheStoredKey()
{
    m_settings->setApiKey(QStringLiteral("super-secret"));

    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    evaluate(tab.get(), QStringLiteral("loadValues()"));
    QCoreApplication::processEvents();

    QObject *shared = sharedKeyField(tab.get());
    QVERIFY(shared);
    QCOMPARE(m_settings->apiKeySet(), true);

    // The user erases the mask.
    shared->setProperty("text", QString());
    shared->setProperty("edited", true);
    evaluate(tab.get(), QStringLiteral("saveValues()"));
    QCoreApplication::processEvents();

    QCOMPARE(m_settings->apiKeySet(), false);
}

// A typed key replaces the stored one.
void TestRuntimeTabExternalQml::aTypedKeyReplacesTheStoredOne()
{
    m_settings->setApiKey(QStringLiteral("super-secret"));

    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    evaluate(tab.get(), QStringLiteral("loadValues()"));
    QCoreApplication::processEvents();

    QObject *shared = sharedKeyField(tab.get());
    QVERIFY(shared);
    shared->setProperty("text", QStringLiteral("new-key"));
    shared->setProperty("edited", true);
    evaluate(tab.get(), QStringLiteral("saveValues()"));
    QCoreApplication::processEvents();

    QCOMPARE(m_settings->apiKey(), QStringLiteral("new-key"));
}

// The switch stands in the table header and governs every key of the tab —
// the shared one and the one of each task.
void TestRuntimeTabExternalQml::theHeaderSwitchRevealsEveryKey()
{
    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QCOMPARE(evaluate(tab.get(), QStringLiteral("revealKeys")).toBool(), false);

    tab->setProperty("revealKeys", true);
    QCoreApplication::processEvents();

    QObject *shared = sharedKeyField(tab.get());
    QVERIFY(shared);
    QCOMPARE(shared->property("revealed").toBool(), true);

    QObject *row = rowForTask(tab.get(), QStringLiteral("blockRecognition"));
    QVERIFY(row);
    QCOMPARE(row->property("revealed").toBool(), true);
}

// The mask is the field's text, so it can be erased — but it must never mix
// with the key the user types.
void TestRuntimeTabExternalQml::theMaskIsNeverMixedWithTheTypedKey()
{
    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QObject *shared = sharedKeyField(tab.get());
    QVERIFY(shared);
    const QString mask = shared->property("maskText").toString();

    QCOMPARE(evaluate(shared, QStringLiteral("plainText('%1')").arg(mask)).toString(), QString());
    QCOMPARE(evaluate(shared, QStringLiteral("plainText('%1abc')").arg(mask)).toString(), QStringLiteral("abc"));
    QCOMPARE(evaluate(shared, QStringLiteral("plainText('abc')")).toString(), QStringLiteral("abc"));
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_runtime_tab_external_qml"));
    TestRuntimeTabExternalQml test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_runtime_tab_external_qml.moc"
