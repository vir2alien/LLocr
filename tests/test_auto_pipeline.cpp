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
    void start(const Scope &) override
    {
        m_started = true;
        m_log->append(id());
        if (m_endImmediately)
            emit finished();
    }
    void stop() override
    {
        m_stopped = true;
        emit finished();
    }
    int progressDone() const override { return m_done; }
    int progressTotal() const override { return m_total; }

    void setSkipReason(const QString &reason) { m_skipReason = reason; }
    void setEndImmediately(bool immediate) { m_endImmediately = immediate; }
    void setProgress(int done, int total)
    {
        m_done = done;
        m_total = total;
    }
    void fail(const StatusMessage &message) { emit failed(message); }
    bool started() const { return m_started; }
    bool stopped() const { return m_stopped; }

private:
    QStringList *m_log;
    QString m_skipReason;
    bool m_endImmediately = true;
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
    void anEmptyScopeIsRefused();
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
    FakeStage second("recognition", "Recognition", &log);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    first.fail(StatusMessage::literal("the model refused"));
    // The failed stage reports itself and the pipeline moves on.
    QCoreApplication::processEvents();
    QCOMPARE(log, QStringList({"markup", "recognition"}));
    QCOMPARE(finishedSpy.count(), 1);
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

void TestAutoPipeline::anEmptyScopeIsRefused()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    AutoPipelineController controller({[] { return QList<int>{}; }, [] { return true; }});
    controller.setStages({&first});
    QVERIFY(!controller.start());
    QVERIFY(log.isEmpty());
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
