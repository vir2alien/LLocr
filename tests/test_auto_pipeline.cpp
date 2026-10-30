#include <QCoreApplication>
#include <QSignalSpy>
#include <QtTest>

#include "app/AutoPipelineController.h"

using namespace llocr;

namespace {

// A stage that records the order and ends when the test tells it to.
class FakeStage : public AutoPipelineStage
{
    Q_OBJECT
public:
    FakeStage(QString id, QString title, QStringList *log) : AutoPipelineStage(id, title), m_log(log) {}

    QString skipReason(const Scope &) const override { return m_skipReason; }
    void start(const Scope &pages) override
    {
        m_started = true;
        m_scope = pages;
        m_log->append(id());
        if (m_endImmediately)
            emit finished();
    }
    void stop() override
    {
        m_stopped = true;
        if (!m_silentStop)
            emit finished();
    }
    int progressDone() const override { return m_done; }
    int progressTotal() const override { return m_total; }

    void setSkipReason(const QString &reason) { m_skipReason = reason; }
    void setEndImmediately(bool immediate) { m_endImmediately = immediate; }
    void setSilentStop(bool silent) { m_silentStop = silent; }
    void setProgress(int done, int total)
    {
        m_done = done;
        m_total = total;
    }
    void fail(const StatusMessage &message) { emit failed(message); }
    void reportFinished() { emit finished(); }
    bool started() const { return m_started; }
    bool stopped() const { return m_stopped; }
    Scope scope() const { return m_scope; }

private:
    QStringList *m_log;
    Scope m_scope;
    QString m_skipReason;
    bool m_endImmediately = true;
    bool m_silentStop = false;
    bool m_started = false;
    bool m_stopped = false;
    int m_done = 0;
    int m_total = 0;
};

}  // namespace

class TestAutoPipeline : public QObject
{
    Q_OBJECT

private slots:
    void theStagesRunInOrder();
    void aSkippedStageIsPassedOver();
    void aFailedStageDoesNotStopTheRun();
    void theRunStopsOnRequest();
    void aStageThatNeverReportsItsStopEndsTheRun();
    void aSkippedStageIsNotStopped();
    void anEmptyScopeIsRefused();
    void aBusyDocumentIsRefused();
    void aFinishedSignalFromAnotherStageIsIgnored();
    void theRunKeepsTheScopeItCapturedAtTheStart();
    void settingTheStagesTwiceReplacesTheFirstList();
    void theProgressComesFromTheRunningStage();
};

void TestAutoPipeline::theStagesRunInOrder()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    FakeStage second("recognition", "Recognition", &log);
    AutoPipelineController controller({[] { return QList<int>{0, 1}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    QCOMPARE(log, QStringList({"markup", "recognition"}));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().first().toBool(), true);
}

void TestAutoPipeline::aSkippedStageIsPassedOver()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    FakeStage second("recognition", "Recognition", &log);
    second.setSkipReason("no model");
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QSignalSpy statusSpy(&controller, &AutoPipelineController::statusRequested);

    QVERIFY(controller.start());
    QCoreApplication::processEvents();       // the skip is reported when the run ends
    QCOMPARE(log, QStringList({"markup"}));  // the skipped stage never ran
    QVERIFY(statusSpy.count() >= 1);         // the reason reached the status line
    QVERIFY(statusSpy.first().first().value<StatusMessage>().text().contains("no model"));
}

void TestAutoPipeline::aFailedStageDoesNotStopTheRun()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    FakeStage second("recognition", "Recognition", &log);
    second.setEndImmediately(false);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QSignalSpy statusSpy(&controller, &AutoPipelineController::statusRequested);
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    QCOMPARE(log, QStringList({"markup"}));  // the first stage is running

    first.fail(StatusMessage::literal("the model refused"));

    // The reason reached the status line and the run moved on to the next stage.
    QCOMPARE(log, QStringList({"markup", "recognition"}));
    QCOMPARE(controller.stageIndex(), 1);
    QCOMPARE(statusSpy.count(), 1);
    QCOMPARE(statusSpy.first().first().value<StatusMessage>().text(), QString("the model refused"));

    second.reportFinished();
    QCoreApplication::processEvents();
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().first().toBool(), true);
}

void TestAutoPipeline::theRunStopsOnRequest()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    FakeStage second("recognition", "Recognition", &log);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});

    QVERIFY(controller.start());
    controller.stop();
    QVERIFY(first.stopped());
    QCoreApplication::processEvents();
    QCOMPARE(log, QStringList({"markup"}));  // the second stage never started
    QVERIFY(!controller.isRunning());
}

void TestAutoPipeline::aStageThatNeverReportsItsStopEndsTheRun()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    first.setSilentStop(true);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first});
    controller.setStopGraceMs(50);
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    controller.stop();
    QVERIFY(first.stopped());
    // The run waits for the stage to wind down instead of hanging on it.
    QVERIFY(controller.isRunning());

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
    QCOMPARE(finishedSpy.first().first().toBool(), false);
    QVERIFY(!controller.isRunning());
}

void TestAutoPipeline::aSkippedStageIsNotStopped()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    FakeStage second("recognition", "Recognition", &log);
    second.setSkipReason("no model");
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    QCOMPARE(log, QStringList({"markup"}));  // the second stage was skipped
    QCOMPARE(controller.stageIndex(), 1);

    controller.stop();
    QVERIFY(!second.stopped());  // the skipped stage never ran, so it is not stopped
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().first().toBool(), false);
}

void TestAutoPipeline::anEmptyScopeIsRefused()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    AutoPipelineController controller({[] { return QList<int>{}; }, [] { return true; }});
    controller.setStages({&first});
    QSignalSpy statusSpy(&controller, &AutoPipelineController::statusRequested);

    QVERIFY(!controller.start());
    QVERIFY(log.isEmpty());
    QCOMPARE(statusSpy.count(), 1);
    QCOMPARE(statusSpy.first().first().value<StatusMessage>().text(), QString("No pages to process"));
}

void TestAutoPipeline::aBusyDocumentIsRefused()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return false; }});
    controller.setStages({&first});
    QSignalSpy statusSpy(&controller, &AutoPipelineController::statusRequested);

    QVERIFY(!controller.start());
    QVERIFY(log.isEmpty());
    QCOMPARE(statusSpy.count(), 1);
    QCOMPARE(statusSpy.first().first().value<StatusMessage>().text(), QString("Another job is running"));
}

void TestAutoPipeline::aFinishedSignalFromAnotherStageIsIgnored()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    FakeStage second("recognition", "Recognition", &log);
    second.setEndImmediately(false);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    QCOMPARE(log, QStringList({"markup"}));

    first.reportFinished();
    QCOMPARE(log, QStringList({"markup", "recognition"}));
    QCOMPARE(controller.stageIndex(), 1);

    first.reportFinished();  // a late signal from the stage that is over
    QCoreApplication::processEvents();
    QCOMPARE(log, QStringList({"markup", "recognition"}));  // the run did not advance
    QCOMPARE(controller.stageIndex(), 1);
    QCOMPARE(finishedSpy.count(), 0);
}

void TestAutoPipeline::theRunKeepsTheScopeItCapturedAtTheStart()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    int calls = 0;
    AutoPipelineController controller({[&calls]() {
                                           calls++;
                                           return calls == 1 ? QList<int>{0, 1} : QList<int>{7};
                                       },
                                       [] { return true; }});
    controller.setStages({&first});

    QVERIFY(controller.start());
    QCOMPARE(calls, 1);  // the provider was asked once
    QCOMPARE(first.scope(), QList<int>({0, 1}));
}

void TestAutoPipeline::settingTheStagesTwiceReplacesTheFirstList()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    FakeStage second("recognition", "Recognition", &log);
    second.setEndImmediately(false);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first});
    controller.setStages({&second});
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    QCOMPARE(log, QStringList({"recognition"}));  // the first list was replaced

    first.reportFinished();  // the replaced stage no longer drives the run
    QCoreApplication::processEvents();
    QCOMPARE(log, QStringList({"recognition"}));
    QCOMPARE(controller.stageIndex(), 0);
    QCOMPARE(finishedSpy.count(), 0);
}

void TestAutoPipeline::theProgressComesFromTheRunningStage()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    first.setProgress(2, 5);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first});

    QVERIFY(controller.start());
    QCOMPARE(controller.progressDone(), 2);
    QCOMPARE(controller.progressTotal(), 5);
    QCOMPARE(controller.stageIndex(), 0);
    QCOMPARE(controller.stageCount(), 1);
}

QTEST_MAIN(TestAutoPipeline)
#include "test_auto_pipeline.moc"
