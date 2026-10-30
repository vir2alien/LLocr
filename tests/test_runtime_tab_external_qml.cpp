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
    void theTabLoadsWithoutWarnings();
    void theFourTasksHaveARow();
    void theCompactModeHidesTheTasks();
    void theTaskEndpointAndAliasAreSaved();
    void theSavedValuesComeBackIntoTheFields();

private:
    void registerModuleTypes();
    std::unique_ptr<QObject> loadTab();
    QVariant evaluate(QObject *scope, const QString &expression);
    QObject *rowAt(QObject *tab, int index);
    QObject *rowForTask(QObject *tab, const QString &task);

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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_runtime_tab_external_qml"));
    TestRuntimeTabExternalQml test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_runtime_tab_external_qml.moc"
