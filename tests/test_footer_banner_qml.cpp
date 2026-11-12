// The footer's diagnostic banner (ADR 306): it must be closable, and its
// buttons must stay against the window's edge.
//
// The banner shows one elided line — the current page's decode error, else the
// check error, else the parser note — plus the count and a «Details…» button.
// Two things were wrong with it. The banner could not be closed: the error
// message lived in the controller, so clearing the log took the button away
// and left the message alone. And the row lost its right-hand filler when the
// message was absent (an invisible item takes no place in a layout), so the
// «Details…» button jumped to the left of the window instead of staying at
// the edge.
//
// The file is read from the source tree, so the test covers the shipped QML.

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>

#include "app/UiController.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

class FakeLog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY logChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY logChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY logChanged)

public:
    int count() const { return m_count; }
    int errorCount() const { return m_errors; }
    int warningCount() const { return m_count - m_errors; }

    void report(bool error)
    {
        ++m_count;
        if (error)
            ++m_errors;
        emit logChanged();
    }

    Q_INVOKABLE void clear()
    {
        if (m_count == 0)
            return;
        m_count = 0;
        m_errors = 0;
        emit logChanged();
    }

    Q_INVOKABLE void copyLog() {}
    Q_INVOKABLE void attachLogView(QObject *) {}

signals:
    void logChanged();

private:
    int m_count = 0;
    int m_errors = 0;
};

class FakeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPageWarning READ currentPageWarning NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString checkErrorMessage READ checkErrorMessage NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString parseWarning READ parseWarning NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY busyChanged)
    Q_PROPERTY(bool projectBusy READ projectBusy NOTIFY busyChanged)

public:
    QString currentPageWarning() const { return m_pageWarning; }
    QString checkErrorMessage() const { return m_checkError; }
    QString parseWarning() const { return m_parseWarning; }
    bool busy() const { return false; }
    bool importing() const { return false; }
    bool projectBusy() const { return false; }

    void setCheckError(const QString &message)
    {
        if (m_checkError == message)
            return;
        m_checkError = message;
        emit diagnosticsChanged();
    }

    void setPageWarning(const QString &message)
    {
        if (m_pageWarning == message)
            return;
        m_pageWarning = message;
        emit diagnosticsChanged();
    }

signals:
    void diagnosticsChanged();
    void busyChanged();

private:
    QString m_pageWarning;
    QString m_checkError;
    QString m_parseWarning;
};

class FakeRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(RuntimeState state READ state NOTIFY stateChanged)
    Q_PROPERTY(BusyState busyState READ busyState NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(bool lockedOut READ lockedOut NOTIFY stateChanged)
    Q_PROPERTY(bool configValid READ configValid NOTIFY stateChanged)
    Q_PROPERTY(bool launchConfigDirty READ launchConfigDirty NOTIFY stateChanged)
    Q_PROPERTY(bool orphanDetected READ orphanDetected NOTIFY stateChanged)
    Q_PROPERTY(QString orphanInfo READ orphanInfo NOTIFY stateChanged)

public:
    enum RuntimeState {
        NotConfigured,
        Stopped,
        Starting,
        Ready,
        Stopping,
        Failed,
    };
    Q_ENUM(RuntimeState)

    enum BusyState {
        Idle,
        StartingRuntime,
        Recognizing,
        StoppingRuntime,
        Downloading,
        Installing,
    };
    Q_ENUM(BusyState)

    RuntimeState state() const { return Stopped; }
    BusyState busyState() const { return Idle; }
    QString statusMessage() const { return QString(); }
    bool lockedOut() const { return false; }
    bool configValid() const { return false; }
    bool launchConfigDirty() const { return false; }
    bool orphanDetected() const { return false; }
    QString orphanInfo() const { return QString(); }

    Q_INVOKABLE void startServer() {}
    Q_INVOKABLE void stopServer() {}
    Q_INVOKABLE void restartServer() {}
    Q_INVOKABLE void terminateOrphan() {}

signals:
    void stateChanged();
};

class FakeSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString connectionMode READ connectionMode NOTIFY connectionModeChanged)

public:
    QString connectionMode() const { return QStringLiteral("managed"); }

signals:
    void connectionModeChanged();
};

class FakeTaskStatus : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE QString text() const { return QString(); }
    Q_INVOKABLE bool running() const { return false; }
};

}  // namespace

class TestFooterBannerQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void theDetailsButtonStaysAtTheEdgeWithoutAMessage();
    void theDetailsButtonStaysAtTheEdgeWithAMessage();
    void theBannerCloses();
    void theBannerReturnsWithANewProblem();
    void theProblemLogStaysReachable();

private:
    std::unique_ptr<QObject> loadFooter();
    QQuickItem *item(const char *objectName) const;
    void click(QQuickItem *button);
    qreal spacing() const;

    QString m_qmlRoot;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<FakeLog> m_log;
    std::unique_ptr<FakeController> m_controller;
    std::unique_ptr<FakeRuntime> m_runtime;
    std::unique_ptr<FakeSettings> m_settingsDouble;
    std::unique_ptr<FakeTaskStatus> m_taskStatus;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_parent = nullptr;
    QObject *m_footer = nullptr;
    QStringList m_warnings;
};

void TestFooterBannerQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    m_settings = std::make_unique<SettingsStore>();
    m_uiController = std::make_unique<UiController>(*m_settings);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    m_log = std::make_unique<FakeLog>();
    m_controller = std::make_unique<FakeController>();
    m_runtime = std::make_unique<FakeRuntime>();
    m_settingsDouble = std::make_unique<FakeSettings>();
    m_taskStatus = std::make_unique<FakeTaskStatus>();

    qmlRegisterSingletonInstance("LLocr", 1, 0, "Log", m_log.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Controller", m_controller.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", m_runtime.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", m_settingsDouble.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "TaskStatus", m_taskStatus.get());

    const QUrl themeUrl = QUrl::fromLocalFile(m_qmlRoot + QStringLiteral("/Theme.qml"));
    qmlRegisterSingletonType(themeUrl, "LLocr", 1, 0, "Theme");

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    m_window = std::make_unique<QQuickWindow>();
    m_parent = new QQuickItem(m_window->contentItem());
    m_parent->setSize(QSizeF(800, 600));
    m_window->resize(800, 600);
    m_window->show();
}

std::unique_ptr<QObject> TestFooterBannerQml::loadFooter()
{
    m_warnings.clear();

    const QString path = QDir(m_qmlRoot).filePath(QStringLiteral("MainWindow/Footer.qml"));
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
    if (auto *footer = qobject_cast<QQuickItem *>(object.get())) {
        footer->setParentItem(m_parent);
        footer->setWidth(800);
    }
    m_footer = object.get();
    // The layout is applied in the polish: let it happen before the test
    // measures anything.
    QTest::qWait(20);
    return object;
}

QQuickItem *TestFooterBannerQml::item(const char *objectName) const
{
    return m_footer ? m_footer->findChild<QQuickItem *>(QString::fromLatin1(objectName)) : nullptr;
}

void TestFooterBannerQml::click(QQuickItem *button)
{
    QVERIFY(button);
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(button->width() / 2.0, button->height() / 2.0)).toPoint());
    QTest::qWait(20);
}

qreal TestFooterBannerQml::spacing() const
{
    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    return theme ? theme->property("spacing").toReal() : 8.0;
}

// The «Details…» button must sit against the window's edge even when the
// banner has no message to show: an invisible label leaves the row without a
// filler, and the button drifts to the left.
void TestFooterBannerQml::theDetailsButtonStaysAtTheEdgeWithoutAMessage()
{
    const std::unique_ptr<QObject> footer = loadFooter();
    QVERIFY2(footer, qPrintable(m_warnings.join(QStringLiteral("; "))));

    m_log->report(true);
    QTest::qWait(20);

    QQuickItem *details = item("diagnosticsDetailsButton");
    QVERIFY2(details, "the banner must carry the «Details…» button");
    QVERIFY(details->isVisible());

    QQuickItem *close = item("diagnosticsCloseButton");
    QVERIFY2(close, "the banner must carry the close button");

    const qreal edge = 800.0 - spacing();
    const qreal closeRight = close->mapToScene(QPointF(close->width(), 0)).x();
    const qreal detailsRight = details->mapToScene(QPointF(details->width(), 0)).x();
    QVERIFY2(qAbs(closeRight - edge) < 1.0, qPrintable(QStringLiteral("the close button must sit against the edge, it is at %1 of %2").arg(closeRight).arg(edge)));
    QVERIFY2(qAbs(detailsRight - (closeRight - close->width() - spacing())) < 1.0,
             qPrintable(QStringLiteral("the «Details…» button must stand next to the close button, it is at %1").arg(detailsRight)));
}

// The same with a message on the banner: the button keeps the edge.
void TestFooterBannerQml::theDetailsButtonStaysAtTheEdgeWithAMessage()
{
    const std::unique_ptr<QObject> footer = loadFooter();
    QVERIFY2(footer, qPrintable(m_warnings.join(QStringLiteral("; "))));

    m_log->report(true);
    m_controller->setCheckError(QStringLiteral("connection refused"));
    QTest::qWait(20);

    QQuickItem *details = item("diagnosticsDetailsButton");
    QVERIFY2(details, "the banner must carry the «Details…» button");
    QVERIFY(details->isVisible());

    QQuickItem *close = item("diagnosticsCloseButton");
    QVERIFY2(close, "the banner must carry the close button");

    const qreal edge = 800.0 - spacing();
    const qreal closeRight = close->mapToScene(QPointF(close->width(), 0)).x();
    const qreal detailsRight = details->mapToScene(QPointF(details->width(), 0)).x();
    QVERIFY2(qAbs(closeRight - edge) < 1.0, qPrintable(QStringLiteral("the close button must sit against the edge, it is at %1 of %2").arg(closeRight).arg(edge)));
    QVERIFY2(qAbs(detailsRight - (closeRight - close->width() - spacing())) < 1.0,
             qPrintable(QStringLiteral("the «Details…» button must stand next to the close button, it is at %1").arg(detailsRight)));
}

// The banner must be closable: the close button takes it away.
void TestFooterBannerQml::theBannerCloses()
{
    const std::unique_ptr<QObject> footer = loadFooter();
    QVERIFY2(footer, qPrintable(m_warnings.join(QStringLiteral("; "))));

    m_log->report(true);
    m_controller->setCheckError(QStringLiteral("connection refused"));
    QTest::qWait(20);

    QQuickItem *banner = item("diagnosticsBanner");
    QVERIFY2(banner, "the banner must be findable");
    QVERIFY(banner->isVisible());

    click(item("diagnosticsCloseButton"));

    QVERIFY2(!banner->isVisible(), "the close button must take the banner away");
}

// A new problem brings the banner back: the dismissal is for the message that
// was on the screen, not for the banner itself.
void TestFooterBannerQml::theBannerReturnsWithANewProblem()
{
    const std::unique_ptr<QObject> footer = loadFooter();
    QVERIFY2(footer, qPrintable(m_warnings.join(QStringLiteral("; "))));

    m_log->report(true);
    m_controller->setCheckError(QStringLiteral("connection refused"));
    QTest::qWait(20);

    QQuickItem *banner = item("diagnosticsBanner");
    QVERIFY(banner);

    click(item("diagnosticsCloseButton"));
    QVERIFY(!banner->isVisible());

    m_log->report(true);
    QTest::qWait(20);
    QVERIFY2(banner->isVisible(), "a new problem must bring the banner back");

    click(item("diagnosticsCloseButton"));
    QVERIFY(!banner->isVisible());

    m_log->clear();
    QTest::qWait(20);
    QVERIFY2(!banner->isVisible(), "clearing the log is not a new problem and must not bring it back");

    m_controller->setCheckError(QStringLiteral("another failure"));
    QTest::qWait(20);
    QVERIFY2(banner->isVisible(), "a new message must bring the banner back");
}

// The closed banner must not take the problem log with it: the log window is
// the only way to read the entries, so the footer keeps a way into it.
void TestFooterBannerQml::theProblemLogStaysReachable()
{
    const std::unique_ptr<QObject> footer = loadFooter();
    QVERIFY2(footer, qPrintable(m_warnings.join(QStringLiteral("; "))));

    m_log->report(true);
    m_controller->setCheckError(QStringLiteral("connection refused"));
    QTest::qWait(20);

    click(item("diagnosticsCloseButton"));

    QQuickItem *entry = item("problemLogEntry");
    QVERIFY2(entry, "the footer must keep a way into the problem log");
    QVERIFY2(entry->isVisible(), "the way into the problem log must be there while the log holds entries");

    m_log->clear();
    QTest::qWait(20);
    QVERIFY2(!entry->isVisible(), "an empty log needs no way in");
}

QTEST_MAIN(TestFooterBannerQml)
#include "test_footer_banner_qml.moc"
