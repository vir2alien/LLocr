#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTranslator>
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
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    bool dark() const { return false; }
    QString language() const { return m_language; }
    void setLanguage(const QString &language)
    {
        if (m_language == language)
            return;
        m_language = language;
        emit languageChanged();
    }

signals:
    void languageChanged();

private:
    QString m_language = QStringLiteral("ru");
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
    void theZoomWordsComeFromTheAppsCatalog();
    void theEditorPageFollowsTheAppsLanguage();

private:
    QStringList m_messages;
    FakeUiController m_uiController;
    // One engine for the whole binary: a singleton instance belongs to the
    // engine that was alive when it was registered.
    QQmlEngine m_engine;
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
    QQmlComponent component(&m_engine, QUrl::fromLocalFile(QDir(QFINDTESTDATA(QStringLiteral("../src/qml"))).filePath(QStringLiteral("MainWindow/MarkdownEditor.qml"))));
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

// The words of the zoom buttons are the app's own: the page takes them from
// the translation catalog, so a new app language brings its words with it
// (ADR 303).
void TestEditorQml::theZoomWordsComeFromTheAppsCatalog()
{
    QTranslator translator;
    QVERIFY2(translator.load(QStringLiteral(LLOCR_QM_FILE)), LLOCR_QM_FILE);
    QVERIFY(QCoreApplication::installTranslator(&translator));

    QQmlComponent component(&m_engine, QUrl::fromLocalFile(QDir(QFINDTESTDATA(QStringLiteral("../src/qml"))).filePath(QStringLiteral("MainWindow/MarkdownEditor.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));

    const QString labels = object->property("zoomLabels").toString();
    QVERIFY2(labels.contains(QString::fromUtf8("Увеличить")), qPrintable(labels));
    QVERIFY2(labels.contains(QString::fromUtf8("Сбросить масштаб")), qPrintable(labels));

    QCoreApplication::removeTranslator(&translator);
}

// The page of the editor speaks the language of the app: the language travels
// in the url of the page (ADR 303).
void TestEditorQml::theEditorPageFollowsTheAppsLanguage()
{
    QQmlComponent component(&m_engine, QUrl::fromLocalFile(QDir(QFINDTESTDATA(QStringLiteral("../src/qml"))).filePath(QStringLiteral("MainWindow/MarkdownEditor.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("language").toString(), QStringLiteral("ru"));

    const auto pageUrl = [object = object.get()]() {
        QString url;
        const auto items = object->findChildren<QQuickItem *>();
        for (QQuickItem *item : items) {
            const QString candidate = item->property("url").toUrl().toString();
            if (candidate.startsWith(QStringLiteral("qrc:/editor/")))
                url = candidate;
        }
        return url;
    };

    QVERIFY2(pageUrl().contains(QStringLiteral("lang=ru")), qPrintable(pageUrl()));

    m_uiController.setLanguage(QStringLiteral("en"));
    QVERIFY2(pageUrl().contains(QStringLiteral("lang=en")), qPrintable(pageUrl()));
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
