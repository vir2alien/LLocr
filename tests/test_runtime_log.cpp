#include <QCoreApplication>
#include <QDateTime>
#include <QQuickTextDocument>
#include <QtTest>

#include "config/SettingsStore.h"
#include "core/RequestJournal.h"
#include "runtime/RuntimeLog.h"
#include "testsettings.h"

using namespace llocr;

class TestRuntimeLog : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

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
};

QTEST_MAIN(TestRuntimeLog)

#include "test_runtime_log.moc"
