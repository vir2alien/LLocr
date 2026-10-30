#include <QCoreApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <utility>

#include "app/AutoPipelineController.h"
#include "app/AutoPipelineStages.h"
#include "app/DocumentModel.h"
#include "app/VerificationPromptStore.h"
#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/RuntimeController.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A stage that records the order and ends when the test tells it to.
class FakeStage : public AutoPipelineStage
{
    Q_OBJECT
public:
    FakeStage(QString name, QString title, QStringList *log) : AutoPipelineStage(title), m_log(log), m_name(std::move(name)) {}

    QString skipReason(const Scope &) const override { return m_skipReason; }
    void start(const Scope &pages) override
    {
        m_started = true;
        m_scope = pages;
        m_log->append(m_name);
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
    QString m_name;
    Scope m_scope;
    QString m_skipReason;
    bool m_endImmediately = true;
    bool m_silentStop = false;
    bool m_started = false;
    bool m_stopped = false;
    int m_done = 0;
    int m_total = 0;
};

// The queue and the dependencies it reads, with the layout counted as busy:
// the queue refuses every list, the state a stage has to survive.
class BusyQueueFixture
{
public:
    explicit BusyQueueFixture(const QTemporaryDir &dir)
    {
        m_settings = std::make_unique<SettingsStore>();
        m_settings->setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        m_settings->setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        m_launchProfiles = std::make_unique<LaunchProfileStore>(*m_settings);
        m_checkProfiles = std::make_unique<RequestProfileStore>(*m_settings, ConnectionRole::BlockRecognition);
        m_fixProfiles = std::make_unique<RequestProfileStore>(*m_settings, ConnectionRole::Fix);
        m_runtime = std::make_unique<RuntimeController>(*m_settings, *m_launchProfiles);
        m_verification = std::make_unique<VerificationPromptStore>(*m_settings);
        m_document = std::make_unique<DocumentModel>();
        m_queue = std::make_unique<VerificationQueueController>(VerificationQueueController::Deps{
            *m_document, *m_verification, *m_checkProfiles, *m_fixProfiles, *m_settings, *m_runtime, [](int, int) { return QImage(); }, [] { return true; }, [](int, int) { return QString(); }, {}});
    }

    VerificationQueueController &queue() { return *m_queue; }
    DocumentModel &document() { return *m_document; }

private:
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<LaunchProfileStore> m_launchProfiles;
    std::unique_ptr<RequestProfileStore> m_checkProfiles;
    std::unique_ptr<RequestProfileStore> m_fixProfiles;
    std::unique_ptr<RuntimeController> m_runtime;
    std::unique_ptr<VerificationPromptStore> m_verification;
    std::unique_ptr<DocumentModel> m_document;
    std::unique_ptr<VerificationQueueController> m_queue;
};

}  // namespace

class TestAutoPipeline : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QImage source(64, 48, QImage::Format_ARGB32);
        source.fill(Qt::white);
        QVERIFY(source.save(m_dir.filePath(QStringLiteral("page.png")), "PNG"));
    }

    void theStagesRunInOrder();
    void aSkippedStageIsPassedOver();
    void aFailedStageDoesNotStopTheRun();
    void theRunStopsOnRequest();
    void theRunWaitsForTheStageToReportItsStop();
    void aStageThatNeverReportsItsStopEndsTheRun();
    void aSecondStopDoesNotExtendTheGrace();
    void aNegativeStopGraceIsRejected();
    void aSkippedStageIsNotStopped();
    void aStopFromTheStateHandlerKeepsTheStageFromStarting();
    void aStopDuringTheTransitionKeepsTheNextStageFromStarting();
    void anEmptyScopeIsRefused();
    void aBusyDocumentIsRefused();
    void aFinishedSignalFromAnotherStageIsIgnored();
    void theRunKeepsTheScopeItCapturedAtTheStart();
    void settingTheStagesTwiceReplacesTheFirstList();
    void theSameStageSetTwiceReportsOneFailure();
    void theStagesCannotBeReplacedDuringARun();
    void theProgressComesFromTheRunningStage();
    void aRefusedWorkListIsAFailure();
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

void TestAutoPipeline::theRunWaitsForTheStageToReportItsStop()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    first.setSilentStop(true);  // the stage winds down and reports later
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first});
    controller.setStopGraceMs(2000);
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    controller.stop();
    QVERIFY(first.stopped());

    // The event loop turns, but the stage has not reported yet: the run waits
    // for it instead of giving up at the next tick.
    QCoreApplication::processEvents();
    QVERIFY(controller.isRunning());
    QCOMPARE(finishedSpy.count(), 0);

    // The stage reports — the run ends.
    first.reportFinished();
    QCoreApplication::processEvents();
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().first().toBool(), false);
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

void TestAutoPipeline::aSecondStopDoesNotExtendTheGrace()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    first.setSilentStop(true);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first});
    controller.setStopGraceMs(300);
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    controller.stop();
    QTest::qWait(200);
    controller.stop();  // the deadline must not move
    QTest::qWait(150);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().first().toBool(), false);
}

void TestAutoPipeline::aNegativeStopGraceIsRejected()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    first.setSilentStop(true);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first});
    controller.setStopGraceMs(-1);  // rejected: the grace stays the default
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    controller.stop();
    QTest::qWait(100);
    QCOMPARE(finishedSpy.count(), 0);  // the run is still waiting for the stage
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

void TestAutoPipeline::aStopFromTheStateHandlerKeepsTheStageFromStarting()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first});
    // The planned QML path: the pipeline's state notification asks to stop.
    QObject::connect(&controller, &AutoPipelineController::stateChanged, &controller, [&controller]() {
        if (controller.stageIndex() >= 0)
            controller.stop();
    });

    QVERIFY(controller.start());
    QVERIFY(!first.started());  // the stop came before the stage began its work
    QVERIFY(!controller.isRunning());
}

void TestAutoPipeline::aStopDuringTheTransitionKeepsTheNextStageFromStarting()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    FakeStage second("recognition", "Recognition", &log);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    // The stop arrives while the run is moving to the next stage.
    QObject::connect(&controller, &AutoPipelineController::stateChanged, &controller, [&controller]() {
        if (controller.stageIndex() == 1)
            controller.stop();
    });

    QVERIFY(controller.start());
    QCOMPARE(log, QStringList({"markup"}));  // the next stage never started
    QVERIFY(!second.started());
    QVERIFY(!controller.isRunning());
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

void TestAutoPipeline::theSameStageSetTwiceReportsOneFailure()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    FakeStage second("recognition", "Recognition", &log);
    second.setEndImmediately(false);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    controller.setStages({&first, &second});  // the same stages once more
    QSignalSpy statusSpy(&controller, &AutoPipelineController::statusRequested);

    QVERIFY(controller.start());
    first.fail(StatusMessage::literal("the model refused"));
    QCOMPARE(statusSpy.count(), 1);  // one failure — one message
    QCOMPARE(statusSpy.first().first().value<StatusMessage>().text(), QString("the model refused"));
    QCOMPARE(log, QStringList({"markup", "recognition"}));
}

void TestAutoPipeline::theStagesCannotBeReplacedDuringARun()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    FakeStage second("recognition", "Recognition", &log);
    second.setEndImmediately(false);
    FakeStage third("fix", "Fix", &log);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QVERIFY(controller.start());

    controller.setStages({&third});  // refused: the live run owns its stages
    QCOMPARE(controller.stageCount(), 2);

    first.reportFinished();
    QCOMPARE(log, QStringList({"markup", "recognition"}));
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

void TestAutoPipeline::aRefusedWorkListIsAFailure()
{
    BusyQueueFixture fixture(m_dir);
    QVERIFY(fixture.document().appendImage(m_dir.filePath(QStringLiteral("page.png"))));
    fixture.document().page(0).recognized = true;
    OcrPage page;
    BoundingBox box;
    box.label = QStringLiteral("text");
    page.boxes = {box};
    fixture.document().page(0).result.pages = {page};

    AutoRecognitionStage stage(fixture.queue(), [] { return true; });
    QSignalSpy failedSpy(&stage, &AutoPipelineStage::failed);
    QSignalSpy finishedSpy(&stage, &AutoPipelineStage::finished);

    stage.start({0});

    // The queue refused the list — the layout holds the document. The stage
    // has to say so: a success would leave the block unrecognized and the run
    // would look clean.
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 1);
    QVERIFY(failedSpy.first().first().value<StatusMessage>().text().contains(QStringLiteral("the queue refused the work")));
}

QTEST_MAIN(TestAutoPipeline)
#include "test_auto_pipeline.moc"
