// The runtime log window: the server's own output on top, the request
// journal below. The two halves are wired to two different sources, and the
// window must show each of them in its own pane. The file is read from the
// source tree, so the test covers the shipped QML.

#include <QDateTime>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QtTest>

#include "config/SettingsStore.h"
#include "core/RequestJournal.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/RuntimeLog.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A stand-in for the UiController singleton: the theme reads the dark flag
// and the language from it.
class FakeUiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark CONSTANT)
    Q_PROPERTY(QString language READ language CONSTANT)

public:
    bool dark() const { return false; }
    QString language() const { return QStringLiteral("en"); }
};

}  // namespace

class TestServerLogWindowQml : public QObject
{
    Q_OBJECT

    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void cleanup();
    void theWindowShowsBothPanes();
    void thePanesShowTheirOwnSource();
    void theFooterTellsTheCountersApart();

private:
    void registerModuleTypes();
    std::unique_ptr<QObject> loadWindow();
    QQuickItem *item(const char *objectName) const;
    QString paneText(const char *objectName) const;

    QString m_qmlRoot;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<RuntimeLog> m_log;
    std::unique_ptr<FakeUiController> m_uiController;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<LlamaServerProcess> m_server;
    QObject *m_window = nullptr;
    QStringList m_warnings;
};

void TestServerLogWindowQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    m_settings = std::make_unique<SettingsStore>();
    m_log = std::make_unique<RuntimeLog>(*m_settings);
    m_uiController = std::make_unique<FakeUiController>();

    qmlRegisterSingletonInstance("LLocr", 1, 0, "RuntimeLog", m_log.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    // The QML module of the app (qt_add_qml_module) turns every file into a
    // type of the LLocr module. The window is loaded from the source tree
    // here, so the same types are registered by hand — otherwise the
    // components it uses (LLOLabel, LLOButton, …) would not resolve.
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

void TestServerLogWindowQml::registerModuleTypes()
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

void TestServerLogWindowQml::cleanup()
{
    RequestJournal::instance().clear();
    m_log->setServer(nullptr);
}

std::unique_ptr<QObject> TestServerLogWindowQml::loadWindow()
{
    m_warnings.clear();

    const QString path = QDir(m_qmlRoot).filePath(QStringLiteral("ServerLogWindow.qml"));
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    if (!component.isReady()) {
        m_warnings.append(component.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(component.create());
    if (!object)
        m_warnings.append(component.errorString());
    m_window = object.get();
    return object;
}

QQuickItem *TestServerLogWindowQml::item(const char *objectName) const
{
    return m_window ? m_window->findChild<QQuickItem *>(QString::fromLatin1(objectName)) : nullptr;
}

QString TestServerLogWindowQml::paneText(const char *objectName) const
{
    QQuickItem *area = item(objectName);
    if (!area)
        return {};
    auto *document = area->property("textDocument").value<QQuickTextDocument *>();
    if (!document)
        return {};
    return document->textDocument()->toPlainText();
}

// The window is one window with two halves: the server's output above, the
// request journal below.
void TestServerLogWindowQml::theWindowShowsBothPanes()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QCOMPARE(item("serverOutputLabel")->property("text").toString(), QStringLiteral("Server output"));
    QCOMPARE(item("journalLabel")->property("text").toString(), QStringLiteral("Request journal"));

    QVERIFY2(item("serverLogArea"), "the window must carry the pane of the server's output");
    QVERIFY2(item("journalLogArea"), "the window must carry the pane of the request journal");
}

// Each pane shows its own source and nothing of the other one.
void TestServerLogWindowQml::thePanesShowTheirOwnSource()
{
    LlamaServerProcess::Options options;
    m_server = std::make_unique<LlamaServerProcess>(options);
    m_log->setServer(m_server.get());
    const bool delivered = QMetaObject::invokeMethod(m_server.get(), "logLineAppended", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("the server said hello")));
    QVERIFY(delivered);

    RequestJournal::Record record;
    record.at = QDateTime::currentDateTime();
    record.endpoint = QStringLiteral("http://example.test/v1/chat/completions");
    record.model = QStringLiteral("m");
    RequestJournal::instance().record(record);

    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    const QString server = paneText("serverLogArea");
    QVERIFY2(server.contains(QStringLiteral("the server said hello")), qPrintable(server));
    QVERIFY2(!server.contains(QStringLiteral("example.test")), "the server's pane must not show the journal");

    const QString journal = paneText("journalLogArea");
    QVERIFY2(journal.contains(QStringLiteral("example.test")), qPrintable(journal));
    QVERIFY2(!journal.contains(QStringLiteral("the server said hello")), "the journal's pane must not show the server's output");
}

// The two counters of the footer must say which source they count.
void TestServerLogWindowQml::theFooterTellsTheCountersApart()
{
    const std::unique_ptr<QObject> window = loadWindow();
    QVERIFY2(window, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QQuickItem *server = item("serverLineCount");
    QQuickItem *journal = item("journalRequestCount");
    QVERIFY2(server, "the footer must count the lines of the server");
    QVERIFY2(journal, "the footer must count the requests of the journal");

    QVERIFY2(server->property("text").toString().startsWith(QStringLiteral("Server")), qPrintable(server->property("text").toString()));
    QVERIFY2(journal->property("text").toString().startsWith(QStringLiteral("Requests")), qPrintable(journal->property("text").toString()));
}

QTEST_MAIN(TestServerLogWindowQml)
#include "test_server_log_window_qml.moc"
