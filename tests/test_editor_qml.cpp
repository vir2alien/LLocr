#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "app/EditorBridge.h"

using namespace llocr;

// The editor wrapper's QML wiring, loaded for real: MarkdownEditor must hand
// the page a WebChannel that carries a live EditorBridge (the page's
// qwebchannel.js looks up channel.objects.editorBridge and stays silent
// forever without it — an editor that renders a toolbar but never receives
// the pushed markdown), and the page must answer with pageReady. Regression
// gate for the bridge resolution inside the view.
namespace {

class FakeUiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark CONSTANT)

public:
    bool dark() const { return false; }
};

QStringList *g_messages = nullptr;

void collectingHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    if (g_messages)
        g_messages->append(message);
}

}  // namespace

class TestEditorQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void theEditorWiringRegistersABridgeAndReachesPageReady();

private:
    QStringList m_messages;
    FakeUiController m_uiController;
};

void TestEditorQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    // The wrapper instantiates its own bridge (no engine-wide singleton): a
    // bare type name inside registeredObjects does not resolve to an object.
    qmlRegisterType<llocr::EditorBridge>("LLocr", 1, 0, "EditorBridge");
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", &m_uiController);

    const QString qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    qmlRegisterSingletonType(QUrl::fromLocalFile(qmlRoot + QStringLiteral("/Theme.qml")), "LLocr", 1, 0, "Theme");

    g_messages = &m_messages;
    qInstallMessageHandler(collectingHandler);
}

void TestEditorQml::theEditorWiringRegistersABridgeAndReachesPageReady()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QDir(QFINDTESTDATA(QStringLiteral("../src/qml"))).filePath(QStringLiteral("MainWindow/MarkdownEditor.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));

    QQuickWindow window;
    auto *item = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(item);
    item->setParentItem(window.contentItem());
    window.resize(760, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // pageReady travels page → bridge → wrapper only when the WebChannel
    // registration resolved; the wrapper sets _channelReady on it.
    QElapsedTimer wait;
    wait.start();
    while (!object->property("_channelReady").toBool() && wait.elapsed() < 30000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!object->property("_channelReady").toBool()) {
        for (const QString &message : m_messages)
            fprintf(stderr, "EDITOR-QML-MSG: %s\n", qPrintable(message));
        fprintf(stderr, "EDITOR-QML-STATE: pageLoaded=%s loadError=%s\n", object->property("_pageLoaded").toBool() ? "true" : "false", qPrintable(object->property("_loadError").toString()));
    }
    QVERIFY(object->property("_channelReady").toBool());
    for (const QString &message : m_messages)
        QVERIFY2(!message.contains(QLatin1String("Cannot register null object to WebChannel")), qPrintable(message));
}

int main(int argc, char *argv[])
{
    // Must run before the QGuiApplication is constructed.
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    TestEditorQml tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_editor_qml.moc"
