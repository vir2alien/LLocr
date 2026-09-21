#include <QtTest>

#include <QAbstractItemModelTester>
#include <QFile>
#include <QGuiApplication>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QPointer>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <memory>

#include "testsettings.h"
#include "app/AppController.h"
#include "app/LaunchProfileStore.h"
#include "app/RecognitionController.h"
#include "app/RequestProfileStore.h"
#include "app/VerificationPromptStore.h"

using namespace llocr;

namespace {
constexpr int kImportTimeoutMs = 10000;

QString fixture(const char *name)
{
    return QFINDTESTDATA(QStringLiteral("fixtures/djvu/") + QString::fromLatin1(name));
}

QList<QColor> colors(int page)
{
    if (page == 1)
        return {Qt::green, Qt::white, Qt::red, Qt::blue};
    if (page == 2)
        return {Qt::cyan, Qt::magenta, Qt::yellow, Qt::black};
    return {Qt::red, Qt::green, Qt::blue, Qt::white};
}

QSize nativeSize(int page)
{
    if (page == 1)
        return {57, 81};
    if (page == 2)
        return {63, 45};
    return {81, 57};
}

void compareQuadrants(const QImage &image, const QList<QColor> &expected)
{
    QVERIFY(!image.isNull());
    for (int i = 0; i < 4; ++i) {
        const QColor actual = image.pixelColor(image.width() * (i % 2 ? 3 : 1) / 4,
                                               image.height() * (i / 2 ? 3 : 1) / 4);
        const QColor color = expected.at(i);
        QVERIFY2(qAbs(actual.red() - color.red()) <= 12
                     && qAbs(actual.green() - color.green()) <= 12
                     && qAbs(actual.blue() - color.blue()) <= 12,
                 qPrintable(QStringLiteral("Quadrant %1: expected %2, got %3")
                                .arg(i).arg(color.name(), actual.name())));
    }
}

void compareWhite(const QImage &image)
{
    QVERIFY(!image.isNull());
    QImage expected(image.size(), QImage::Format_RGB32);
    expected.fill(Qt::white);
    QCOMPARE(image.convertToFormat(QImage::Format_RGB32), expected);
}

// Hold the global pool's only worker so even the tiny fixtures remain pending.
// Shared semaphore ownership and a finite worker wait also make early assertion
// returns safe. No decoder/controller test hook or timing-dependent large file.
class ImportWorkerGate
{
public:
    ImportWorkerGate()
        : m_previousMax(QThreadPool::globalInstance()->maxThreadCount())
    {
        auto *pool = QThreadPool::globalInstance();
        pool->setMaxThreadCount(1);
        pool->start([state = m_state] {
            state->entered.release();
            state->release.tryAcquire(1, 15000);
        });
    }

    ~ImportWorkerGate()
    {
        release();
        QThreadPool::globalInstance()->setMaxThreadCount(m_previousMax);
    }

    bool waitUntilHeld() { return m_state->entered.tryAcquire(1, 3000); }
    void release()
    {
        if (!m_released) {
            m_released = true;
            m_state->release.release();
        }
    }

private:
    struct State {
        QSemaphore entered;
        QSemaphore release;
    };
    std::shared_ptr<State> m_state = std::make_shared<State>();
    int m_previousMax;
    bool m_released = false;
};

// Direct connections are intentional: queued observers would hide a signal
// accidentally emitted by a worker. Only record GUI data after checking affinity.
class ImportSignals : public QObject
{
public:
    explicit ImportSignals(AppController &controller)
    {
        const auto observe = [this] {
            if (QThread::currentThread() != thread())
                wrongThread.store(true);
        };
        for (auto signal : {&AppController::busyChanged, &AppController::importingChanged,
                            &AppController::configChanged, &AppController::statusChanged,
                            &AppController::documentChanged, &AppController::pageChanged,
                            &AppController::imageChanged, &AppController::resultChanged,
                            &AppController::docRevisionChanged,
                            &AppController::imageRevisionChanged}) {
            connect(&controller, signal, this, observe, Qt::DirectConnection);
        }
        connect(&controller, &AppController::importingChanged, this, [this, &controller] {
            if (QThread::currentThread() == thread())
                importingStates.append(controller.importing());
        }, Qt::DirectConnection);
        connect(&controller, &AppController::documentChanged, this, [this, &controller] {
            if (QThread::currentThread() == thread())
                committedCounts.append(controller.pageCount());
        }, Qt::DirectConnection);
        auto *model = qobject_cast<PageListModel *>(controller.pageModel());
        connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, observe, Qt::DirectConnection);
        connect(model, &QAbstractItemModel::rowsInserted, this, observe, Qt::DirectConnection);
        connect(model, &QAbstractItemModel::dataChanged, this, observe, Qt::DirectConnection);
        connect(model, &QAbstractItemModel::modelReset, this, observe, Qt::DirectConnection);
    }

    std::atomic_bool wrongThread = false;
    QList<bool> importingStates;
    QList<int> committedCounts;
};
} // namespace

class TestAppImport : public QObject
{
    Q_OBJECT

private:
    TestSettingsIsolation m_settingsIsolation;
    QTemporaryDir m_dir;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<LaunchProfileStore> m_launchProfiles;
    std::unique_ptr<RequestProfileStore> m_requestProfiles;
    std::unique_ptr<RequestProfileStore> m_checkRequestProfiles;
    std::unique_ptr<VerificationPromptStore> m_verification;
    std::unique_ptr<RuntimeController> m_runtime;
    std::unique_ptr<AppController> m_controller;
    QString m_raster;
    QString m_malformed;
    QString m_multipage;

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        QSettings().clear();
        m_settings = std::make_unique<SettingsStore>();
        m_settings->setRuntimeRootDir(m_dir.filePath(QStringLiteral("runtime")));
        m_settings->setRuntimeModelsDir(m_dir.filePath(QStringLiteral("models")));
        m_settings->setMode(ConnectionMode::External);
        // A valid model makes the recognition guard meaningful; an unsupported
        // local URL prevents any network traffic even if that guard regresses.
        m_settings->setModelName(QStringLiteral("import-guard-test"));
        m_settings->setBaseUrl(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("no-server"))).toString());
        m_launchProfiles = std::make_unique<LaunchProfileStore>(*m_settings);
        m_requestProfiles = std::make_unique<RequestProfileStore>(*m_settings);
        m_checkRequestProfiles = std::make_unique<RequestProfileStore>(*m_settings);
        m_verification = std::make_unique<VerificationPromptStore>(*m_settings);
        m_runtime = std::make_unique<RuntimeController>(*m_settings, *m_launchProfiles);
        m_controller = std::make_unique<AppController>(*m_settings, *m_runtime,
                                                       *m_requestProfiles,
                                                       *m_checkRequestProfiles,
                                                       *m_verification);

        m_multipage = fixture("multipage.djvu");
        QVERIFY(!m_multipage.isEmpty());
        m_raster = m_dir.filePath(QStringLiteral("raster.png"));
        QImage raster(39, 27, QImage::Format_RGB32);
        raster.fill(Qt::yellow);
        QVERIFY(raster.save(m_raster));
        m_malformed = m_dir.filePath(QStringLiteral("malformed.djvu"));
        QFile malformed(m_malformed);
        QVERIFY(malformed.open(QIODevice::WriteOnly));
        const QByteArray bytes("Not a DjVu document\n");
        QCOMPARE(malformed.write(bytes), bytes.size());
    }

    void cleanup()
    {
        m_controller.reset();
        // Drain abandoned static workers before deleting fixtures/prerequisites.
        QVERIFY2(QThreadPool::globalInstance()->waitForDone(kImportTimeoutMs),
                 "Import worker did not finish after controller destruction");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        m_runtime.reset();
        m_requestProfiles.reset();
        m_launchProfiles.reset();
        m_settings.reset();
    }

    void recognitionSkipsPlaceholdersWithoutRequestingImages()
    {
        int providerCalls = 0;
        QList<int> skippedPages;
        RecognitionController recognition(*m_settings, *m_runtime, *m_requestProfiles,
            [&providerCalls](int, QString &) {
                ++providerCalls;
                QImage image(800, 1000, QImage::Format_RGB32);
                image.fill(Qt::white);
                return image;
            }, nullptr, [&skippedPages](int index) {
                skippedPages.append(index);
                return true;
            });
        QSignalSpy resultSpy(&recognition, &RecognitionController::rawResultReady);
        QSignalSpy statusSpy(&recognition, &RecognitionController::statusRequested);
        QSignalSpy busySpy(&recognition, &RecognitionController::busyChanged);
        QVERIFY(resultSpy.isValid());
        QVERIFY(statusSpy.isValid());
        QVERIFY(busySpy.isValid());

        // External resolution is immediate; the file URL in init() prevents
        // HTTP even on regression. A skipped page must never reach the provider.
        recognition.startCurrent(0, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!recognition.busy(), kImportTimeoutMs);
        QCOMPARE(providerCalls, 0);
        QCOMPARE(skippedPages, QList<int>({0}));
        QCOMPARE(resultSpy.count(), 0);
        QCOMPARE(busySpy.count(), 2);
        QCOMPARE(statusSpy.count(), 1);
        QCOMPARE(statusSpy.first().first().toString(),
                 QStringLiteral("Page 1 is a blank replacement for an unreadable page; recognition skipped."));

        skippedPages.clear();
        statusSpy.clear();
        busySpy.clear();
        recognition.startAll(3);
        QTRY_VERIFY_WITH_TIMEOUT(!recognition.busy(), kImportTimeoutMs);
        QCOMPARE(providerCalls, 0);
        QCOMPARE(skippedPages, QList<int>({0, 1, 2}));
        QCOMPARE(resultSpy.count(), 0);
        QCOMPARE(busySpy.count(), 2);
        QCOMPARE(statusSpy.count(), 1);
        QCOMPARE(statusSpy.first().first().toString(),
                 QStringLiteral("Recognition finished. Skipped 3 unreadable page(s)."));
    }

    void mixedQueuePreservesOrderAndGuiSignals()
    {
        auto &controller = *m_controller;
        auto *model = qobject_cast<PageListModel *>(controller.pageModel());
        QVERIFY(model);
        QAbstractItemModelTester modelTester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        ImportSignals observations(controller);
        QSignalSpy busySpy(&controller, &AppController::busyChanged);
        QSignalSpy insertedSpy(model, &QAbstractItemModel::rowsInserted);
        int heartbeats = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            if (controller.importing())
                ++heartbeats;
        });
        heartbeat.start(1);

        ImportWorkerGate gate;
        QVERIFY(gate.waitUntilHeld());
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_multipage),
                              QUrl::fromLocalFile(m_malformed), QUrl::fromLocalFile(m_multipage)});
        QVERIFY(controller.importing());
        QVERIFY(!controller.busy());
        QTRY_VERIFY_WITH_TIMEOUT(heartbeats >= 3, 3000);
        QVERIFY(controller.importing());
        QCOMPARE(controller.pageCount(), 1); // Raster committed; DjVu cannot run yet.
        gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        heartbeat.stop();

        QCOMPARE(observations.importingStates, QList<bool>({true, false}));
        QCOMPARE(observations.committedCounts, QList<int>({1, 4, 7}));
        QVERIFY(!observations.wrongThread.load());
        QCOMPARE(busySpy.count(), 0);
        QCOMPARE(insertedSpy.count(), 3);
        for (int i = 0; i < insertedSpy.count(); ++i) {
            QCOMPARE(insertedSpy.at(i).at(1).toInt(), i == 0 ? 0 : (i == 1 ? 1 : 4));
            QCOMPARE(insertedSpy.at(i).at(2).toInt(), i == 0 ? 0 : (i == 1 ? 3 : 6));
        }
        QCOMPARE(controller.pageCount(), 7);
        QCOMPARE(model->rowCount(), 7);
        QCOMPARE(controller.currentPage(), 0);
        QVERIFY(controller.hasImage());
        QVERIFY(!controller.hasResult());
        QVERIFY(controller.canRecognize());
        QCOMPARE(controller.statusMessage(), QStringLiteral("Added 3 file(s), 7 page(s); 1 file(s) skipped."));
        for (int row = 0; row < 7; ++row) {
            const QModelIndex index = model->index(row, 0);
            QCOMPARE(model->data(index, PageListModel::PageIndexRole).toInt(), row);
            QCOMPARE(model->data(index, PageListModel::CurrentRole).toBool(), row == 0);
            QVERIFY(!model->data(index, PageListModel::RecognizedRole).toBool());
            QVERIFY(!model->data(index, PageListModel::EditedRole).toBool());
            QVERIFY(!model->data(index, PageListModel::HasDuplicatesRole).toBool());
        }
        // Revisit both copies after cache eviction, not just freshly imported thumbs.
        for (int row : {0, 1, 2, 3, 4, 5, 6, 1, 5, 0}) {
            QString error;
            const QImage image = controller.pageImage(row, &error);
            QVERIFY2(!image.isNull(), qPrintable(error));
            QVERIFY(error.isEmpty());
            const QList<QColor> expected = row == 0 ? QList<QColor>(4, Qt::yellow) : colors((row - 1) % 3);
            QCOMPARE(image.size(), row == 0 ? QSize(39, 27) : nativeSize((row - 1) % 3));
            compareQuadrants(image, expected);
            compareQuadrants(controller.pageThumbnail(row), expected);
        }
        controller.setCurrentPage(5);
        QCOMPARE(controller.currentPage(), 5);
        compareQuadrants(controller.currentImage(), colors(1));
        QVERIFY(model->data(model->index(5, 0), PageListModel::CurrentRole).toBool());
        QVERIFY(!model->data(model->index(0, 0), PageListModel::CurrentRole).toBool());

        const QString output = m_dir.filePath(QStringLiteral("unrecognized.txt"));
        QVERIFY(!controller.exportPages(QUrl::fromLocalFile(output), 0));
        QVERIFY(!controller.exporting());
        QVERIFY(!QFile::exists(output));
    }

    void damagedPagesImportWithWarningsAndKeepQueueOrder()
    {
        auto &controller = *m_controller;
        const QString path = fixture("bad-second-page.djvu");
        QVERIFY(!path.isEmpty());
        const auto prepared = DocumentModel::prepareDjVu(path);
        QVERIFY2(prepared.error.isEmpty(), qPrintable(prepared.error));
        QCOMPARE(prepared.warnings.size(), 1);
        const QString warning = prepared.warnings.first();
        QVERIFY(warning.contains(path));
        QVERIFY(warning.contains(QStringLiteral("page 2")));

        const int propertyIndex = controller.metaObject()->indexOfProperty("currentPageWarning");
        QVERIFY(propertyIndex >= 0);
        const QMetaProperty pageWarning = controller.metaObject()->property(propertyIndex);
        QCOMPARE(pageWarning.metaType(), QMetaType::fromType<QString>());
        QVERIFY(pageWarning.isReadable());
        QVERIFY(!pageWarning.isWritable());
        QCOMPARE(pageWarning.notifySignal(), QMetaMethod::fromSignal(&AppController::pageChanged));
        QVERIFY(controller.currentPageWarning().isEmpty());

        auto *model = qobject_cast<PageListModel *>(controller.pageModel());
        QVERIFY(model);
        QAbstractItemModelTester modelTester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        ImportSignals observations(controller);
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(path),
                              QUrl::fromLocalFile(m_malformed), QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(observations.importingStates, QList<bool>({true, false}));
        QCOMPARE(observations.committedCounts, QList<int>({1, 4, 7}));
        QVERIFY(!observations.wrongThread.load());
        QCOMPARE(controller.pageCount(), 7);
        QCOMPARE(model->rowCount(), 7);
        const QString status = controller.statusMessage();
        const QString summary = QStringLiteral("Added 3 file(s), 7 page(s); 1 file(s) skipped.");
        QVERIFY2(status.startsWith(summary), qPrintable(status));
        QVERIFY2(status.contains(warning), qPrintable(status));
        // Check the warning count separately from file/page totals and from
        // the original warning's source path and one-based page number.
        const QString warningSummary = status.mid(summary.size(), status.indexOf(warning) - summary.size());
        QVERIFY2(warningSummary.contains(QRegularExpression(QStringLiteral("\\b2\\b"))), qPrintable(status));
        QVERIFY(controller.currentPageWarning().isEmpty());

        QSignalSpy pageSpy(&controller, &AppController::pageChanged);
        for (int row : {1, 2, 3, 4, 5, 6, 0, 2, 5}) {
            pageSpy.clear();
            controller.setCurrentPage(row);
            QCOMPARE(controller.currentPage(), row);
            QVERIFY(!pageSpy.isEmpty());
            const bool damaged = row == 2 || row == 5;
            QCOMPARE(controller.currentPageWarning(), damaged ? warning : QString());
            QCOMPARE(pageWarning.read(&controller).toString(), controller.currentPageWarning());
            QString error = QStringLiteral("stale error");
            const QImage image = controller.pageImage(row, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            if (damaged) {
                QCOMPARE(image.size(), QSize(800, 1000));
                compareWhite(image);
                compareWhite(controller.currentImage());
                compareWhite(controller.pageThumbnail(row));
            } else {
                const QList<QColor> expected = row == 0 ? QList<QColor>(4, Qt::yellow) : colors((row - 1) % 3);
                QCOMPARE(image.size(), row == 0 ? QSize(39, 27) : nativeSize((row - 1) % 3));
                compareQuadrants(image, expected);
                compareQuadrants(controller.pageThumbnail(row), expected);
            }
        }

        QVERIFY(controller.movePage(2, 0));
        QVERIFY(controller.removePage(1)); // Remove the raster, not an original DjVu page.
        QCOMPARE(controller.pageCount(), 6);
        for (int row = 0; row < 6; ++row) {
            controller.setCurrentPage(row);
            const bool damaged = row == 0 || row == 4;
            QCOMPARE(controller.currentPageWarning(), damaged ? warning : QString());
            if (damaged) {
                compareWhite(controller.currentImage());
            } else {
                const int source = row == 1 || row == 3 ? 0 : 2;
                QCOMPARE(controller.currentImage().size(), nativeSize(source));
                compareQuadrants(controller.currentImage(), colors(source));
            }
        }
        QVERIFY(controller.removePage(4));
        QVERIFY(controller.removePage(0));
        for (int row = 0; row < 4; ++row) {
            controller.setCurrentPage(row);
            QVERIFY(controller.currentPageWarning().isEmpty());
            compareQuadrants(controller.currentImage(), colors(row % 2 == 0 ? 0 : 2));
        }

        controller.openFiles({QUrl::fromLocalFile(m_multipage)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 7);
        QCOMPARE(controller.statusMessage(), QStringLiteral("Added 1 file(s), 3 page(s)."));
        QVERIFY(controller.currentPageWarning().isEmpty());
    }

    void importingGuardsMutatingActions()
    {
        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 2);
        QVERIFY(controller.canRecognize());
        ImportSignals observations(controller);
        QSignalSpy busySpy(&controller, &AppController::busyChanged);
        QSignalSpy runtimeBusySpy(m_runtime.get(), &RuntimeController::busyStateChanged);

        ImportWorkerGate gate;
        QVERIFY(gate.waitUntilHeld());
        controller.openFiles({QUrl::fromLocalFile(m_multipage)});
        QVERIFY(controller.importing());
        QVERIFY(!controller.busy());
        QVERIFY(!controller.canRecognize());
        const QString status = controller.statusMessage();
        for (int attempt = 0; attempt < 3; ++attempt) {
            controller.openFiles({QUrl::fromLocalFile(m_raster)});
            controller.recognizeCurrent();
            controller.recognizeAll();
            QVERIFY(!controller.removePage(0));
            QVERIFY(!controller.movePage(0, 1));
            QCOMPARE(controller.pageCount(), 2);
            QCOMPARE(controller.statusMessage(), status);
            QVERIFY(!controller.busy());
            QTest::qWait(5);
        }
        QCOMPARE(observations.importingStates, QList<bool>({true}));
        QVERIFY(observations.committedCounts.isEmpty());
        QCOMPARE(busySpy.count(), 0);
        QCOMPARE(runtimeBusySpy.count(), 0);
        gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 5);
        QCOMPARE(observations.importingStates, QList<bool>({true, false}));
        QVERIFY(!observations.wrongThread.load());
        QVERIFY(controller.canRecognize());
        QVERIFY(controller.movePage(2, 0));
        compareQuadrants(controller.pageImage(0), colors(0));
        QVERIFY(controller.removePage(0));
        QCOMPARE(controller.pageCount(), 4);
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 5);
    }

    void failedImportsClearStateAndAllowRetry_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("malformed") << QStringLiteral("malformed");
        QTest::newRow("missing") << QStringLiteral("missing");

    }

    void failedImportsClearStateAndAllowRetry()
    {
        QFETCH(QString, kind);
        const QString path = kind == QStringLiteral("malformed") ? m_malformed
            : m_dir.filePath(QStringLiteral("missing.djvu"));
        QVERIFY(!path.isEmpty());
        auto &controller = *m_controller;
        ImportSignals observations(controller);
        controller.openFiles({QUrl::fromLocalFile(path)});
        QVERIFY(controller.importing());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(observations.importingStates, QList<bool>({true, false}));
        QVERIFY(observations.committedCounts.isEmpty());
        QVERIFY(!observations.wrongThread.load());
        QCOMPARE(controller.pageCount(), 0);
        QCOMPARE(qobject_cast<PageListModel *>(controller.pageModel())->rowCount(), 0);
        QVERIFY(!controller.hasImage());
        QVERIFY(controller.currentPageWarning().isEmpty());
        QVERIFY(!controller.busy());
        QVERIFY2(controller.statusMessage().contains(path), qPrintable(controller.statusMessage()));

        controller.openFiles({QUrl::fromLocalFile(m_multipage)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 3);
        QCOMPARE(observations.importingStates, QList<bool>({true, false, true, false}));
        compareQuadrants(controller.pageImage(2), colors(2));

        controller.setCurrentPage(1);
        const qint64 imageKey = controller.currentImage().cacheKey();
        const qint64 thumbKey = controller.pageThumbnail(1).cacheKey();
        observations.committedCounts.clear();
        controller.openFiles({QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 3);
        QCOMPARE(qobject_cast<PageListModel *>(controller.pageModel())->rowCount(), 3);
        QCOMPARE(controller.currentPage(), 1);
        QCOMPARE(controller.currentImage().cacheKey(), imageKey);
        QCOMPARE(controller.pageThumbnail(1).cacheKey(), thumbKey);
        QVERIFY(controller.currentPageWarning().isEmpty());
        QVERIFY(observations.committedCounts.isEmpty());
        QVERIFY(!observations.wrongThread.load());
        QVERIFY2(controller.statusMessage().contains(path), qPrintable(controller.statusMessage()));
        for (int row = 0; row < 3; ++row)
            compareQuadrants(controller.pageImage(row), colors(row));
    }

    void destructionWithOutstandingImport_data()
    {
        QTest::addColumn<bool>("holdWorker");
        QTest::newRow("queued-worker-outlives-controller") << true;
        QTest::newRow("worker-launched-before-destruction") << false;
    }

    void destructionWithOutstandingImport()
    {
        QFETCH(bool, holdWorker);
        std::unique_ptr<ImportWorkerGate> gate;
        if (holdWorker) {
            gate = std::make_unique<ImportWorkerGate>();
            QVERIFY(gate->waitUntilHeld());
        }
        ImportSignals observations(*m_controller);
        QPointer<AppController> weak(m_controller.get());
        m_controller->openFiles({QUrl::fromLocalFile(m_multipage), QUrl::fromLocalFile(m_raster),
                                 QUrl::fromLocalFile(m_multipage)});
        QVERIFY(m_controller->importing());
        m_controller.reset();
        QVERIFY(weak.isNull());
        if (gate)
            gate->release();
        QVERIFY2(QThreadPool::globalInstance()->waitForDone(kImportTimeoutMs),
                 "Detached import worker did not finish");
        // Dispatch stale watcher events/queued next-file callbacks, if any.
        QTest::qWait(20);
        QCOMPARE(observations.importingStates, QList<bool>({true}));
        QVERIFY(observations.committedCounts.isEmpty());
        QVERIFY(!observations.wrongThread.load());
        m_controller = std::make_unique<AppController>(*m_settings, *m_runtime,
                                                       *m_requestProfiles,
                                                       *m_checkRequestProfiles,
                                                       *m_verification);
        m_controller->openFiles({QUrl::fromLocalFile(m_multipage)});
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        QCOMPARE(m_controller->pageCount(), 3);
        compareQuadrants(m_controller->pageImage(1), colors(1));
    }
};

int main(int argc, char *argv[])
{
    // ExportRenderer only creates timers until render() is called. No WebEngine
    // initialization or Chromium process is needed for import/empty export.
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_app_import"));
    TestAppImport test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_app_import.moc"
