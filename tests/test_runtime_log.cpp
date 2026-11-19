#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QtTest>
#include <QUrl>

#include "config/SettingsStore.h"
#include "core/RequestJournal.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/RuntimeLog.h"
#include "testsettings.h"

using namespace llocr;

class TestRuntimeLog : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

    static void recordMarker(int index)
    {
        RequestJournal::Record record;
        record.at = QDateTime::currentDateTime();
        record.endpoint = QStringLiteral("http://example.test/v1/marker-%1").arg(index);
        record.model = QStringLiteral("m");
        RequestJournal::instance().record(record);
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_runtime_log"));
    }

    void cleanup() { RequestJournal::instance().clear(); }

    // The window shows both sources at once: the server's own output and the
    // client's request journal. They never stand for each other.
    void theTwoSourcesAreSeparate()
    {
        SettingsStore settings;
        RuntimeLog log(settings);

        RequestJournal::Record record;
        record.at = QDateTime::currentDateTime();
        record.endpoint = QStringLiteral("http://example.test/v1/chat/completions");
        record.model = QStringLiteral("m");
        RequestJournal::instance().record(record);

        QVERIFY(log.journalLog().contains(QStringLiteral("example.test")));
        QVERIFY(log.journalLineCount() > 0);
        // The server has never run: its own output is empty.
        QVERIFY(log.serverLog().isEmpty());
    }

    // "Clear view" empties both halves of the window.
    void clearingEmptiesBothSources()
    {
        SettingsStore settings;
        RuntimeLog log(settings);

        RequestJournal::Record record;
        record.at = QDateTime::currentDateTime();
        record.endpoint = QStringLiteral("http://example.test/v1/chat/completions");
        RequestJournal::instance().record(record);
        QVERIFY(!log.journalLog().isEmpty());

        log.clearServerLog();

        QVERIFY(log.journalLog().isEmpty());
        QCOMPARE(log.journalLineCount(), 0);
        QVERIFY(log.serverLog().isEmpty());
    }

    // The journal keeps the last 500 records, so past that cap every new
    // record pushes an old one off the front. The pane must follow the
    // journal: it shows the newest records, not the ones already dropped.
    void theJournalPaneFollowsTheCap()
    {
        SettingsStore settings;
        RuntimeLog log(settings);

        QQmlEngine engine;
        QQmlComponent pane(&engine);
        pane.setData(QByteArrayLiteral("import QtQuick\nTextEdit { }"), QUrl());
        QVERIFY2(pane.isReady(), qPrintable(pane.errorString()));
        std::unique_ptr<QObject> area(pane.create());
        QVERIFY(area);
        auto *view = area->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(view);
        log.attachJournalView(view);

        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 505; ++i)
            recordMarker(i);
        const qint64 elapsed = timer.elapsed();

        const QString text = view->textDocument()->toPlainText();
        qInfo("the journal pane over 505 records took %lld ms", elapsed);

        QVERIFY2(text.contains(QStringLiteral("marker-504")), qPrintable(QStringLiteral("the newest record is missing, the pane holds %1 lines").arg(text.count(QLatin1Char('\n')) + 1)));
        QVERIFY2(!text.contains(QStringLiteral("marker-0")), "the pane still shows a record the journal dropped");
        QCOMPARE(text, log.journalLog());
    }

    // "Copy log" takes both halves of the window: the server's own output and
    // the journal, separated by an empty line.
    void theCopyHoldsBothSources()
    {
        SettingsStore settings;
        RuntimeLog log(settings);

        LlamaServerProcess::Options options;
        LlamaServerProcess server(options);
        log.setServer(&server);
        const bool delivered = QMetaObject::invokeMethod(&server, "logLineAppended", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("the server said hello")));
        QVERIFY(delivered);

        recordMarker(3);

        const QString expected = QStringLiteral("the server said hello") + QLatin1String("\n\n") + log.journalLog();
        QCOMPARE(log.combinedLog(), expected);

        log.copyServerLog();
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
            qInfo("this platform has no clipboard: the composition is checked through combinedLog()");
            return;
        }
        QCOMPARE(clipboard->text(), expected);
    }
};

QTEST_MAIN(TestRuntimeLog)

#include "test_runtime_log.moc"
