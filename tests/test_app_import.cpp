#include <QtTest>

#include <atomic>
#include <memory>
#include <QAbstractItemModelTester>
#include <QColor>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QHostAddress>
#include <QMetaProperty>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QRegularExpression>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <set>

#include "app/AppController.h"
#include "app/BoxListModel.h"
#include "app/ProblemLog.h"
#include "app/RecognitionController.h"
#include "app/VerificationPromptStore.h"
#include "config/RequestProfileStore.h"
#include "core/StatusMessage.h"
#include "runtime/LaunchProfileStore.h"
#include "testsettings.h"

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

// A generated three-page PDF replaces the DjVu fixture wherever the point is
// the import pipeline, not the DjVu decoder: three pages, four colour
// quadrants each, one page size per page. The DjVu cases stay as the
// format-specific coverage.
constexpr double kPdfDpi = 300.0;  // must match DocumentModel's render DPI

QSize pdfPixelSize(const QSize &points)
{
    return {qRound(points.width() / 72.0 * kPdfDpi), qRound(points.height() / 72.0 * kPdfDpi)};
}

bool writeTestPdf(const QString &path)
{
    // The page layout must be set *before* the page it applies to is started:
    // QPdfWriter ignores a layout change once painting of that page began.
    const auto layoutFor = [](int page) {
        // Zero margins so the painter's origin is the page corner and the
        // rendered quadrants land where they are painted.
        return QPageLayout(QPageSize(QSizeF(nativeSize(page)), QPageSize::Point), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0), QPageLayout::Point);
    };

    QPdfWriter writer(path);
    writer.setResolution(72);  // device units == PDF points
    writer.setPageLayout(layoutFor(0));
    QPainter painter;
    if (!painter.begin(&writer))
        return false;
    for (int page = 0; page < 3; ++page) {
        if (page > 0) {
            writer.setPageLayout(layoutFor(page));
            writer.newPage();
        }
        const QSizeF points(nativeSize(page));
        painter.fillRect(QRectF(0, 0, points.width(), points.height()), Qt::white);
        const QList<QColor> pageColors = colors(page);
        for (int i = 0; i < 4; ++i) {
            painter.fillRect(QRectF((i % 2) * points.width() / 2, (i / 2) * points.height() / 2, points.width() / 2, points.height() / 2), pageColors.at(i));
        }
    }
    painter.end();
    return true;
}

void compareQuadrants(const QImage &image, const QList<QColor> &expected)
{
    QVERIFY(!image.isNull());
    for (int i = 0; i < 4; ++i) {
        const QColor actual = image.pixelColor(image.width() * (i % 2 ? 3 : 1) / 4, image.height() * (i / 2 ? 3 : 1) / 4);
        const QColor color = expected.at(i);
        QVERIFY2(qAbs(actual.red() - color.red()) <= 12 && qAbs(actual.green() - color.green()) <= 12 && qAbs(actual.blue() - color.blue()) <= 12,
                 qPrintable(QStringLiteral("Quadrant %1: expected %2, got %3").arg(i).arg(color.name(), actual.name())));
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
    ImportWorkerGate() : m_previousMax(QThreadPool::globalInstance()->maxThreadCount())
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
        for (auto signal : {&AppController::busyChanged,
                            &AppController::importingChanged,
                            &AppController::configChanged,
                            &AppController::statusChanged,
                            &AppController::documentChanged,
                            &AppController::pageChanged,
                            &AppController::imageChanged,
                            &AppController::resultChanged,
                            &AppController::docRevisionChanged,
                            &AppController::imageRevisionChanged}) {
            connect(&controller, signal, this, observe, Qt::DirectConnection);
        }
        connect(
            &controller,
            &AppController::importingChanged,
            this,
            [this, &controller] {
                if (QThread::currentThread() == thread())
                    importingStates.append(controller.importing());
            },
            Qt::DirectConnection);
        connect(
            &controller,
            &AppController::documentChanged,
            this,
            [this, &controller] {
                if (QThread::currentThread() == thread())
                    committedCounts.append(controller.pageCount());
            },
            Qt::DirectConnection);
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
}  // namespace

// Minimal loopback chat endpoint: answers POST /v1/chat/completions with a
// fixed det-token reply, so the real recognition pipeline produces real boxes.
// Used to drive the AppController-level block-removal contract end to end.
// It speaks all three protocols the app sends: the wrapped <|det|>…<|/det|>
// stream the OCR models emit, the decision model's /v1/systemone (probability
// is configurable — below the 0.5 default threshold every block mismatches),
// and the block-recognition transcription (identified by its system message).
// Request counting is what makes "this block was not asked again" observable.
class DetTokenChatServer : public QObject
{
public:
    explicit DetTokenChatServer(QObject *parent = nullptr) : QObject(parent) { connect(&m_server, &QTcpServer::newConnection, this, &DetTokenChatServer::onNewConnection); }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    int requests() const { return m_requests; }

    int systemoneRequests() const { return m_systemoneRequests; }

    qreal systemoneProbability = 0.97;
    bool systemoneBroken = false;  // reply without an "answers" object → every judge fails
    QString transcription = QStringLiteral("Re-recognized block text");

private:
    // The body says which protocol the caller speaks, so the head alone cannot
    // route the request.
    static bool requestComplete(const QByteArray &buffer)
    {
        static const QByteArrayView kContentLength = "Content-Length:";
        const qsizetype headEnd = buffer.indexOf("\r\n\r\n");
        if (headEnd < 0)
            return false;
        const QByteArray head = buffer.left(headEnd);
        const qsizetype at = head.indexOf(kContentLength);
        if (at < 0)
            return true;
        const int length = head.mid(at + kContentLength.size()).trimmed().split('\r').first().toInt();
        return buffer.size() - headEnd - 4 >= length;
    }

    void onNewConnection()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                m_buffers[socket] += socket->readAll();
                if (!requestComplete(m_buffers[socket]))
                    return;
                ++m_requests;
                socket->write(reply(m_buffers[socket]));
                socket->flush();
                socket->disconnectFromHost();
                m_buffers.remove(socket);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    QByteArray reply(const QByteArray &request) const
    {
        QByteArray body;
        if (request.contains("POST /v1/systemone")) {
            // The decision answer: P(match) in the {"noul": x} spelling.
            ++const_cast<DetTokenChatServer *>(this)->m_systemoneRequests;
            body =
                systemoneBroken
                    ? QJsonDocument(QJsonObject{{"error", QJsonObject{{"message", "no such endpoint"}}}}).toJson(QJsonDocument::Compact)
                    : QJsonDocument(QJsonObject{{"answers", QJsonObject{{"match", QJsonObject{{"noul", systemoneProbability}}}}}, {"usage", QJsonObject{{"input_tokens", 42}, {"output_tokens", 0}}}})
                          .toJson(QJsonDocument::Compact);
        } else {
            // Chat completions: the wrapped <|det|>…<|/det|> stream for the OCR
            // prompt (no system message), the block transcription otherwise;
            // the model streams newlines as the two characters `\n`.
            const QString content = request.contains("\"role\":\"system\"") ? transcription
                                                                            : QStringLiteral("<|det|>title [115, 101, 273, 117]<|/det|>1. Introduction\\n"
                                                                                             "<|det|>text [112, 132, 884, 309]<|/det|>Second block text");
            body = QJsonDocument(QJsonObject{
                                     {"id", "cmpl-test"},
                                     {"object", "chat.completion"},
                                     {"choices", QJsonArray{QJsonObject{{"index", 0}, {"message", QJsonObject{{"role", "assistant"}, {"content", content}}}}}},
                                 })
                       .toJson(QJsonDocument::Compact);
        }
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    int m_requests = 0;
    int m_systemoneRequests = 0;
};

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
    std::unique_ptr<ProblemLog> m_problems;
    std::unique_ptr<AppController> m_controller;
    QString m_raster;
    QString m_pdf;
    QString m_broken;
    QString m_multipage;
    QString m_malformed;

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
        m_problems = std::make_unique<ProblemLog>();
        m_controller = std::make_unique<AppController>(*m_settings, *m_runtime, *m_requestProfiles, *m_checkRequestProfiles, *m_verification);
        m_controller->setProblemLog(m_problems.get());

        // Fixtures that exist in every build: a raster, a generated multi-page
        // PDF and an unreadable file. The import pipeline coverage must not
        // depend on an optional codec being installed (ADR 99).
        m_raster = m_dir.filePath(QStringLiteral("raster.png"));
        QImage raster(39, 27, QImage::Format_RGB32);
        raster.fill(Qt::yellow);
        QVERIFY(raster.save(m_raster));
        m_pdf = m_dir.filePath(QStringLiteral("multipage.pdf"));
        QVERIFY2(writeTestPdf(m_pdf), "cannot write the test PDF fixture");
        m_broken = m_dir.filePath(QStringLiteral("broken.png"));
        QFile broken(m_broken);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        const QByteArray brokenBytes("Not an image\n");
        QCOMPARE(broken.write(brokenBytes), brokenBytes.size());

#ifdef LLOCR_HAVE_DJVU
        m_multipage = fixture("multipage.djvu");
        QVERIFY(!m_multipage.isEmpty());
        m_malformed = m_dir.filePath(QStringLiteral("malformed.djvu"));
        QFile malformed(m_malformed);
        QVERIFY(malformed.open(QIODevice::WriteOnly));
        const QByteArray bytes("Not a DjVu document\n");
        QCOMPARE(malformed.write(bytes), bytes.size());
#endif
    }

    void cleanup()
    {
        m_controller.reset();
        // Drain abandoned static workers before deleting fixtures/prerequisites.
        QVERIFY2(QThreadPool::globalInstance()->waitForDone(kImportTimeoutMs), "Import worker did not finish after controller destruction");
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
        RecognitionController recognition(
            *m_settings,
            *m_runtime,
            *m_requestProfiles,
            [&providerCalls](int, QString &) {
                ++providerCalls;
                QImage image(800, 1000, QImage::Format_RGB32);
                image.fill(Qt::white);
                return image;
            },
            nullptr,
            [&skippedPages](int index, bool) {
                skippedPages.append(index);
                return PageSkip::Unreadable;
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
        QCOMPARE(statusSpy.first().first().value<StatusMessage>().text(), QStringLiteral("Page 1 is a blank replacement for an unreadable page; recognition skipped."));

        skippedPages.clear();
        statusSpy.clear();
        busySpy.clear();
        recognition.startAll(3);
        QTRY_VERIFY_WITH_TIMEOUT(!recognition.busy(), kImportTimeoutMs);
        QCOMPARE(providerCalls, 0);
        QCOMPARE(skippedPages, QList<int>({0, 1, 2}));
        QCOMPARE(resultSpy.count(), 0);
        // Nothing was left to recognize, so the run ends without ever becoming
        // busy: resolving a connection here would start the managed server and
        // load a model for no page.
        QCOMPARE(busySpy.count(), 0);
        QCOMPARE(statusSpy.count(), 1);
        QCOMPARE(statusSpy.first().first().value<StatusMessage>().text(), QStringLiteral("Nothing to recognize: 3 unreadable page(s)"));
    }

    // A batch run must not redo pages that already have a result, and a run with
    // nothing left to do must not even resolve a connection (that would start the
    // managed server and load a model for no work). The single-page run keeps the
    // opposite meaning: re-running the OCR for the page you are looking at.
    void recognizeAllSkipsPagesThatAlreadyHaveAResult()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setAutoCheck(false);

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_pdf)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 4, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        controller.recognizeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), kImportTimeoutMs);
        QVERIFY2(controller.hasResult(), qPrintable(controller.statusMessage()));
        const QString allFinished = controller.statusMessage();
        QVERIFY(!allFinished.contains(QStringLiteral("already recognized")));

        // All four pages now carry a result, so a second batch has no work.
        QSignalSpy busySpy(&controller, &AppController::busyChanged);
        QSignalSpy statusSpy(&controller, &AppController::statusChanged);
        controller.recognizeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), kImportTimeoutMs);
        QCOMPARE(busySpy.count(), 0);  // never went busy: no connection was resolved
        QVERIFY(controller.statusMessage().contains(QStringLiteral("Nothing to recognize")));
        QVERIFY(controller.statusMessage().contains(QStringLiteral("4 page(s) already recognized")));
        QVERIFY(statusSpy.count() > 0);

        // Recognizing the current page explicitly re-runs it despite the result.
        controller.recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), kImportTimeoutMs);
        QCOMPARE(busySpy.count(), 2);
        QVERIFY(controller.resultText().contains(QStringLiteral("1. Introduction")));
    }

    // The skip rule itself, isolated from the document: a batch run sends only the
    // pages without a result, a single-page run sends its page whatever its
    // state, and a batch with nothing left never reaches the model at all.
    void theBatchSkipRuleOnlySkipsPagesWithAResult()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setAutoCheck(false);

        std::set<int> withResult{0, 2, 3};
        QList<int> askedFor;
        RecognitionController recognition(
            *m_settings,
            *m_runtime,
            *m_requestProfiles,
            [&askedFor](int index, QString &) {
                askedFor.append(index);
                return QImage(20, 20, QImage::Format_RGB32);
            },
            nullptr,
            [&withResult](int index, bool batch) { return batch && withResult.contains(index) ? PageSkip::AlreadyRecognized : PageSkip::None; });

        // Pages 0, 2 and 3 already have a result, so only page 1 is sent.
        recognition.startAll(4);
        QTRY_VERIFY_WITH_TIMEOUT(!recognition.busy(), kImportTimeoutMs);
        QCOMPARE(askedFor, QList<int>({1}));

        // The single-page run ignores the rule: page 2 is recognized again.
        askedFor.clear();
        recognition.startCurrent(2, 4);
        QTRY_VERIFY_WITH_TIMEOUT(!recognition.busy(), kImportTimeoutMs);
        QCOMPARE(askedFor, QList<int>({2}));

        // Nothing left to do: the run must end without asking for a single page
        // (and without resolving a connection, which is what would start the
        // managed server for work there is none).
        withResult = {0, 1, 2, 3};
        askedFor.clear();
        QSignalSpy statusSpy(&recognition, &RecognitionController::statusRequested);
        QSignalSpy busySpy(&recognition, &RecognitionController::busyChanged);
        recognition.startAll(4);
        QVERIFY(!recognition.busy());
        QCOMPARE(askedFor, QList<int>());
        QCOMPARE(busySpy.count(), 0);
        QCOMPARE(statusSpy.count(), 1);
        QCOMPARE(statusSpy.first().first().value<StatusMessage>().text(), QStringLiteral("Nothing to recognize: 4 page(s) already recognized"));
    }

    // Cancelling a batch: the files already committed stay, the rest are not
    // opened, and the queue finishes normally (importing goes false) so the UI
    // is not left stuck in "importing".
    void importCanBeCancelled()
    {
        auto &controller = *m_controller;
        QSignalSpy progressSpy(&controller, &AppController::importProgressChanged);

        // Ten copies of the same PDF: enough that the timer-driven chain can be
        // cut in the middle.
        QVariantList files;
        for (int i = 0; i < 10; ++i)
            files << QUrl::fromLocalFile(m_pdf);

        controller.openFiles(files);
        QVERIFY(controller.importing());
        QCOMPARE(controller.importProgressTotal(), 10);
        // The first raster/PDF is appended inline, so the counter may already
        // have ticked by the time openFiles returns; it must not have run away.
        QVERIFY(controller.importProgressDone() < 10);

        // Cancel from inside the chain, once at least one file has landed.
        int doneAtCancel = 0;
        connect(&controller, &AppController::importProgressChanged, this, [&] {
            if (controller.importProgressDone() >= 3 && controller.importing()) {
                controller.cancelImport();
                doneAtCancel = controller.importProgressDone();
            }
        });
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QVERIFY(doneAtCancel >= 3);
        QVERIFY(doneAtCancel < 10);

        // Everything committed before the cancel is kept, and nothing after.
        QCOMPARE(controller.pageCount(), doneAtCancel * 3);
        QCOMPARE(controller.importProgressDone(), 10);  // the bar completes, it does not hang
        QVERIFY(controller.statusMessage().contains(QStringLiteral("Import stopped")));

        // Cancelling when nothing is running is a no-op.
        controller.cancelImport();
        QVERIFY(!controller.importing());
    }

    // Mixed multi-file import on the *synchronous* path: images and PDFs are
    // appended on the GUI thread (only DjVu is decoded on a worker,
    // AppController::importNextFile), so this case needs no fixture codec and
    // runs in every build.
    void mixedImportCommitsFilesInOrder()
    {
        auto &controller = *m_controller;
        auto *model = qobject_cast<PageListModel *>(controller.pageModel());
        QVERIFY(model);
        QAbstractItemModelTester modelTester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        ImportSignals observations(controller);
        QSignalSpy insertedSpy(model, &QAbstractItemModel::rowsInserted);
        QSignalSpy busySpy(&controller, &AppController::busyChanged);

        // raster (1 page), PDF (3), unreadable file, PDF (3) again
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_pdf), QUrl::fromLocalFile(m_broken), QUrl::fromLocalFile(m_pdf)});
        QVERIFY(controller.importing());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

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
        }
        // Revisit the pages after the full-image cache evicted some of them.
        for (int row : {0, 1, 2, 3, 4, 5, 6, 1, 5, 0}) {
            QString error;
            const QImage image = controller.pageImage(row, &error);
            QVERIFY2(!image.isNull(), qPrintable(error));
            QVERIFY(error.isEmpty());
            const QList<QColor> expected = row == 0 ? QList<QColor>(4, Qt::yellow) : colors((row - 1) % 3);
            QCOMPARE(image.size(), row == 0 ? QSize(39, 27) : pdfPixelSize(nativeSize((row - 1) % 3)));
            compareQuadrants(image, expected);
            compareQuadrants(controller.pageThumbnail(row), expected);
        }
        controller.setCurrentPage(5);
        QCOMPARE(controller.currentPage(), 5);
        compareQuadrants(controller.pageImage(controller.currentPage()), colors(1));

        const QString output = m_dir.filePath(QStringLiteral("unrecognized.txt"));
        QVERIFY(!controller.exportPages(QUrl::fromLocalFile(output), 0));
        QVERIFY(!controller.exporting());
        QVERIFY(!QFile::exists(output));
    }

#ifdef LLOCR_HAVE_DJVU
    // The following cases exercise the *asynchronous* import path, which today
    // only DjVu takes (a worker decodes and prepares the document). They need
    // the optional codec, so they are compiled in only with it.
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
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_multipage), QUrl::fromLocalFile(m_malformed), QUrl::fromLocalFile(m_multipage)});
        QVERIFY(controller.importing());
        QVERIFY(!controller.busy());
        QTRY_VERIFY_WITH_TIMEOUT(heartbeats >= 3, 3000);
        QVERIFY(controller.importing());
        QCOMPARE(controller.pageCount(), 1);  // Raster committed; DjVu cannot run yet.
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
        compareQuadrants(controller.pageImage(controller.currentPage()), colors(1));
        QVERIFY(model->data(model->index(5, 0), PageListModel::CurrentRole).toBool());
        QVERIFY(!model->data(model->index(0, 0), PageListModel::CurrentRole).toBool());

        const QString output = m_dir.filePath(QStringLiteral("unrecognized.txt"));
        QVERIFY(!controller.exportPages(QUrl::fromLocalFile(output), 0));
        QVERIFY(!controller.exporting());
        QVERIFY(!QFile::exists(output));
    }

    // DjVu-specific: a page the decoder cannot read becomes a blank replacement
    // and the import reports per-page warnings. Requires the optional codec, so
    // it is the one case that only exists in a DjVu-enabled build.
#ifdef LLOCR_HAVE_DJVU
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
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(path), QUrl::fromLocalFile(m_malformed), QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(observations.importingStates, QList<bool>({true, false}));
        QCOMPARE(observations.committedCounts, QList<int>({1, 4, 7}));
        QVERIFY(!observations.wrongThread.load());
        QCOMPARE(controller.pageCount(), 7);
        QCOMPARE(model->rowCount(), 7);
        const QString status = controller.statusMessage();
        const QString summary = QStringLiteral("Added 3 file(s), 7 page(s); 1 file(s) skipped.");
        QVERIFY2(status.startsWith(summary), qPrintable(status));
        // The status line keeps the count and nothing else: the decoder's own
        // paragraph lives in the problem log, because joined onto the status
        // line it did not fit the footer at all (ADR 119).
        QVERIFY2(!status.contains(warning), qPrintable(status));
        QVERIFY2(status.contains(QRegularExpression(QStringLiteral("\\b2\\b"))), qPrintable(status));
        QVERIFY2(status.contains(QStringLiteral("problem log")), qPrintable(status));
        // Both unreadable pages of the two copies, plus the malformed file.
        QCOMPARE(m_problems->warningCount(), 2);
        QCOMPARE(m_problems->errorCount(), 1);
        const QString logText = m_problems->logText();
        QVERIFY2(logText.contains(warning), qPrintable(logText));
        QVERIFY2(logText.contains(m_malformed), qPrintable(logText));
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
                compareWhite(controller.pageImage(controller.currentPage()));
                compareWhite(controller.pageThumbnail(row));
            } else {
                const QList<QColor> expected = row == 0 ? QList<QColor>(4, Qt::yellow) : colors((row - 1) % 3);
                QCOMPARE(image.size(), row == 0 ? QSize(39, 27) : nativeSize((row - 1) % 3));
                compareQuadrants(image, expected);
                compareQuadrants(controller.pageThumbnail(row), expected);
            }
        }

        QVERIFY(controller.movePage(2, 0));
        QVERIFY(controller.removePage(1));  // Remove the raster, not an original DjVu page.
        QCOMPARE(controller.pageCount(), 6);
        for (int row = 0; row < 6; ++row) {
            controller.setCurrentPage(row);
            const bool damaged = row == 0 || row == 4;
            QCOMPARE(controller.currentPageWarning(), damaged ? warning : QString());
            if (damaged) {
                compareWhite(controller.pageImage(controller.currentPage()));
            } else {
                const int source = row == 1 || row == 3 ? 0 : 2;
                QCOMPARE(controller.pageImage(controller.currentPage()).size(), nativeSize(source));
                compareQuadrants(controller.pageImage(controller.currentPage()), colors(source));
            }
        }
        QVERIFY(controller.removePage(4));
        QVERIFY(controller.removePage(0));
        for (int row = 0; row < 4; ++row) {
            controller.setCurrentPage(row);
            QVERIFY(controller.currentPageWarning().isEmpty());
            compareQuadrants(controller.pageImage(controller.currentPage()), colors(row % 2 == 0 ? 0 : 2));
        }

        controller.openFiles({QUrl::fromLocalFile(m_multipage)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 7);
        QCOMPARE(controller.statusMessage(), QStringLiteral("Added 1 file(s), 3 page(s)."));
        QVERIFY(controller.currentPageWarning().isEmpty());
    }

    // Since thumbnails became lazy the import no longer decodes anything, so a
    // page that renders blank is only discovered when it is shown. The user must
    // still hear about it, exactly once, however often the strip repaints.
    void unrenderablePageIsReportedOnce()
    {
        // A DjVu whose INFO chunk is valid but which carries no encoded image
        // chunks: the page dictionary reads fine, the render does not.
        const QString infoOnly = m_dir.filePath(QStringLiteral("info-only.djvu"));
        QFile source(fixture("wide-info.djvu"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QByteArray bytes = source.readAll();
        QVERIFY(bytes.size() > 27);
        bytes[24] = 0;
        bytes[25] = 81;
        bytes[26] = 0;
        bytes[27] = 57;
        QFile out(infoOnly);
        QVERIFY(out.open(QIODevice::WriteOnly));
        QCOMPARE(out.write(bytes), bytes.size());
        out.close();

        auto &controller = *m_controller;
        m_problems->clear();
        controller.openFiles({QUrl::fromLocalFile(infoOnly)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 1);
        // The import itself has nothing to report: it never decoded the page.
        QCOMPARE(m_problems->count(), 0);

        // Asking for the thumbnail triggers the render, the failure, and the
        // queued report on the GUI thread.
        QImage thumb = controller.pageThumbnail(0);
        QTRY_VERIFY_WITH_TIMEOUT(m_problems->count() == 1, 5000);
        const QString first = m_problems->logText();
        QVERIFY2(first.contains(QStringLiteral("could not be rendered")), qPrintable(first));
        QVERIFY2(first.contains(QStringLiteral("Page 1")), qPrintable(first));

        // Repaints must not multiply the entry.
        for (int i = 0; i < 5; ++i) {
            thumb = controller.pageThumbnail(0);
            QVERIFY(!thumb.isNull());
        }
        QTest::qWait(50);
        QCOMPARE(m_problems->count(), 1);

        // The strip still gets a usable placeholder rather than a hole.
        QVERIFY(!thumb.isNull());
        compareWhite(thumb);
    }
#endif  // LLOCR_HAVE_DJVU

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
#endif  // LLOCR_HAVE_DJVU (asynchronous import path)

    // A diagnostic must not be able to grow the footer (ADR 119): the status
    // line keeps the count, the log keeps the reason. This is the case that
    // actually happens on every build — a file that cannot be opened.
    void aSkippedFileGoesToTheLogNotTheStatusLine()
    {
        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_broken)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        // One line, no path in it.
        QCOMPARE(controller.statusMessage(), QStringLiteral("Added 1 file(s), 1 page(s); 1 file(s) skipped."));
        QVERIFY(!controller.statusMessage().contains(m_broken));

        // The reason, at error severity, where a window can show it in full.
        QCOMPARE(m_problems->count(), 1);
        QCOMPARE(m_problems->errorCount(), 1);
        QCOMPARE(m_problems->warningCount(), 0);
        QVERIFY2(m_problems->logText().contains(m_broken), qPrintable(m_problems->logText()));
    }

    // The log coalesces its notifications. A book with hundreds of unreadable
    // pages must not wake the UI — and re-render the whole log — hundreds of
    // times, and it must not grow the process without limit either.
    void theProblemLogCoalescesAndStaysBounded()
    {
        ProblemLog log;
        QSignalSpy changed(&log, &ProblemLog::logChanged);

        for (int i = 0; i < 50; ++i)
            log.report(StatusMessage::translate("TestAppImport", "Broken page %1.").arg(i));
        QCOMPARE(log.count(), 50);
        QCOMPARE(changed.count(), 0);

        QTRY_VERIFY_WITH_TIMEOUT(changed.count() == 1, 2000);
        QCOMPARE(changed.count(), 1);

        for (int i = 0; i < 5000; ++i)
            log.report(StatusMessage::literal(QStringLiteral("flood %1").arg(i)));
        QCOMPARE(log.count(), 2000);
        // The oldest entries fell off, and the log says so instead of pretending
        // it never happened.
        const QString text = log.logText();
        QVERIFY2(!text.contains(QStringLiteral("flood 0")), qPrintable(text.left(200)));
        QVERIFY2(text.contains(QStringLiteral("flood 4999")), qPrintable(text.left(200)));
        QVERIFY2(text.contains(QStringLiteral("dropped")), qPrintable(text.left(200)));

        log.clear();
        QCOMPARE(log.count(), 0);
        QVERIFY(log.logText().isEmpty());
        QCOMPARE(log.errorCount(), 0);
        QCOMPARE(log.warningCount(), 0);
    }

    // An entry that is already in the user's language (a server reply, a file
    // path) is kept verbatim; one we author keeps its key, so a language switch
    // reaches the log too (ADR 114).
    void theProblemLogRendersOnRead()
    {
        ProblemLog log;
        log.report(StatusMessage::literal(QStringLiteral("raw server text")));
        log.report(StatusMessage::translate("TestAppImport", "Page %1 is blank.").arg(7), ProblemLog::Error);

        QCOMPARE(log.count(), 2);
        QCOMPARE(log.warningCount(), 1);
        QCOMPARE(log.errorCount(), 1);
        const QString text = log.logText();
        QVERIFY2(text.contains(QStringLiteral("raw server text")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Page 7 is blank.")), qPrintable(text));

        // The severity is part of the line, so a reader can tell them apart.
        QCOMPARE(text.split(QLatin1Char('\n')).size(), 2);
        QVERIFY(text.startsWith(QStringLiteral("warning: ")));
    }

    // A language switch must reach text that is already on screen. The status
    // line used to hold the *translated* string, so it kept the previous
    // language until something else overwrote it (ADR 114).
    void retranslateRendersTheStatusLineInTheNewLanguage()
    {
        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.statusMessage(), QStringLiteral("Added 1 file(s), 1 page(s)."));

        // The tests link no .qm, so the language switch is simulated with a
        // translator that knows two of this app's keys.
        class FakeRu : public QTranslator
        {
        public:
            QString translate(const char *context, const char *sourceText, const char *, int) const override
            {
                if (qstrcmp(context, "AppController") != 0)
                    return QString();
                if (qstrcmp(sourceText, "Added %1 file(s), %2 page(s).") == 0)
                    return QStringLiteral("Добавлено файлов: %1, страниц: %2");
                if (qstrcmp(sourceText, "Markdown (*.md)") == 0)
                    return QStringLiteral("Markdown RU (*.md)");
                return QString();
            }
        } translator;
        QVERIFY(QCoreApplication::installTranslator(&translator));

        // The message was set *before* the switch: only a message that keeps its
        // key and renders on read can follow the language.
        QSignalSpy retranslateSpy(&controller, &AppController::retranslateRequested);
        controller.retranslate();
        QCOMPARE(retranslateSpy.count(), 1);
        QCOMPARE(controller.statusMessage(), QStringLiteral("Добавлено файлов: 1, страниц: 1"));

        // exportNameFilters was CONSTANT: QML read it once and never again.
        QCOMPARE(controller.exportNameFilters().value(0), QStringLiteral("Markdown RU (*.md)"));

        QCoreApplication::removeTranslator(&translator);
    }

    // The preview render must not block the GUI thread (ADR 118): a DjVu page can
    // take seconds to decode, and the image provider used to do it inline, which
    // froze the window. previewImage() answers with what it has (or nothing) and
    // renders on a worker; pageImageReady then bumps imageRevision.
    void previewRenderKeepsTheEventLoopRunning()
    {
        auto &controller = *m_controller;
        // A big page, so decoding is long enough to measure.
        const QString bigPath = m_dir.filePath(QStringLiteral("big.png"));
        QImage big(3000, 4000, QImage::Format_RGB32);
        big.fill(Qt::white);
        QVERIFY(big.save(bigPath));

        controller.openFiles({QUrl::fromLocalFile(bigPath)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 1);

        // Baseline tick so the first interval is measured, like the probe test.
        QTimer heartbeat;
        int ticks = 0;
        qint64 maxGapMs = 0;
        qint64 lastTick = QDateTime::currentMSecsSinceEpoch();
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            maxGapMs = std::max(maxGapMs, now - lastTick);
            lastTick = now;
            ++ticks;
        });
        heartbeat.start(5);
        QTest::qWait(60);
        lastTick = QDateTime::currentMSecsSinceEpoch();
        maxGapMs = 0;

        QSignalSpy readySpy(&controller, &AppController::pageImageReady);
        // The first call cannot have a full image yet — that is the point.
        QVERIFY(controller.previewImage(0).isNull());
        QVERIFY(controller.previewRendering(0));

        QTRY_VERIFY_WITH_TIMEOUT(!controller.previewRendering(0), 20000);
        QVERIFY2(readySpy.count() >= 1, "the worker never published a page image");
        const QImage rendered = controller.previewImage(0);
        QVERIFY(!rendered.isNull());
        QCOMPARE(rendered.size(), QSize(3000, 4000));
        // A second call is answered from the cache, without a worker.
        QVERIFY(!controller.previewRendering(0));
        QCOMPARE(controller.previewImage(0).size(), QSize(3000, 4000));

        QVERIFY2(ticks > 0, "the timer never fired");
        QVERIFY2(maxGapMs < 500,
                 qPrintable(QStringLiteral("longest gap between timer ticks: %1 ms "
                                           "while rendering a page (%2 ticks total)")
                                .arg(maxGapMs)
                                .arg(ticks)));
    }

    // A rendered page belongs to the page it was rendered from, not to the
    // document as a whole. Appending must keep it — that is the whole point of
    // the batch fast path, and re-decoding page 0 for every imported file is
    // what made a 1000-file import render the same page a thousand times. Any
    // change that *replaces* what sits at an index must invalidate it.
    void previewRenderIsDroppedWhenTheDocumentChanges()
    {
        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QVERIFY(controller.previewImage(0).isNull());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.previewRendering(0), 20000);
        const QImage rendered = controller.previewImage(0);
        QVERIFY(!rendered.isNull());

        // Appending: page 0 is still the same raster, so its image must survive.
        controller.openFiles({QUrl::fromLocalFile(m_pdf)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QVERIFY(controller.pageCount() > 1);
        QCOMPARE(controller.previewImage(0).cacheKey(), rendered.cacheKey());

        // Removing page 0 shifts every later index, so the cached image would
        // now be served for the wrong page and must be gone.
        QVERIFY(controller.removePage(0));
        QVERIFY2(controller.previewImage(0).isNull(), "a page image from the previous document was still served");
    }

    // Regression: the document is the source of truth for blocks. Deleting a
    // block through AppController must mutate the page (so the rebuilt text and
    // the export lose it) and mirror the row into the view model. Deleting it
    // through the view model alone used to leave the document, the export and the
    // verification queue untouched, and the box came back on the next page switch.
    void blockRemovalUpdatesDocumentAndView()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setAutoCheck(false);

        m_controller->openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        // Recognition is refused while an import is still running.
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        m_controller->recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), kImportTimeoutMs);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);
        const QString before = m_controller->resultText();
        QVERIFY(before.contains(QStringLiteral("1. Introduction")));
        QVERIFY(before.contains(QStringLiteral("Second block text")));

        m_controller->setSelectedBoxIndex(1);
        QVERIFY(m_controller->removeBlock(1));

        QCOMPARE(m_controller->selectedBoxIndex(), -1);
        QCOMPARE(boxes->rowCount(), 1);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::LabelRole).toString(), QStringLiteral("title"));
        const QString after = m_controller->resultText();
        QVERIFY(after.contains(QStringLiteral("1. Introduction")));
        QVERIFY(!after.contains(QStringLiteral("Second block text")));
        QVERIFY(m_controller->currentPageEdited());

        // A refused removal must change nothing.
        QVERIFY(!m_controller->removeBlock(7));
        QVERIFY(!m_controller->removeBlock(-1));
        QCOMPARE(boxes->rowCount(), 1);
        QCOMPARE(m_controller->resultText(), after);

        // The remaining block is still deletable and leaves an empty page.
        QVERIFY(m_controller->removeBlock(0));
        QCOMPARE(boxes->rowCount(), 0);
        QVERIFY(!m_controller->resultText().contains(QStringLiteral("Introduction")));
    }

    // The block ↔ text mapping the selection sync builds on: after a det-token
    // recognition every box maps to its character range in the page text; a
    // manual edit switches the mapping to best-effort (intact blocks stay
    // tracked, the edited block drops out) and Revert restores exact ranges.
    void blockTextRangesTrackBoxesAndManualEdits()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setAutoCheck(false);

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        controller.recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), kImportTimeoutMs);
        QVERIFY2(controller.hasResult(), qPrintable(controller.statusMessage()));

        QCOMPARE(controller.resultText(), QStringLiteral("## 1. Introduction\n\nSecond block text"));
        QVERIFY(controller.blockTextMapped());

        QCOMPARE(controller.blockTextRange(0), QList<int>({0, 18}));
        QCOMPARE(controller.blockTextRange(1), QList<int>({20, 17}));
        QVERIFY(controller.blockTextRange(2).isEmpty());

        controller.setSelectedBoxIndex(0);
        QCOMPARE(controller.selectedBlockTextRange(), QVariantList({0, 18}));
        controller.setSelectedBoxIndex(1);
        QCOMPARE(controller.selectedBlockTextRange(), QVariantList({20, 17}));
        controller.setSelectedBoxIndex(-1);
        QVERIFY(controller.selectedBlockTextRange().isEmpty());

        QCOMPARE(controller.boxIndexForTextPosition(0), 0);
        QCOMPARE(controller.boxIndexForTextPosition(17), 0);
        QCOMPARE(controller.boxIndexForTextPosition(19), -1);  // the blank separator line
        QCOMPARE(controller.boxIndexForTextPosition(20), 1);
        QCOMPARE(controller.boxIndexForTextPosition(controller.resultText().length()), 1);

        // A manual edit keeps the mapping in best-effort mode: the intact
        // block stays tracked (shifted), the edited block drops out.
        controller.setCurrentPageText(QStringLiteral("## 1. Preface\n\nSecond block text"));
        QVERIFY(controller.blockTextMapped());
        QVERIFY(controller.blockTextRange(0).isEmpty());
        QCOMPARE(controller.blockTextRange(1), QList<int>({15, 17}));
        QCOMPARE(controller.boxIndexForTextPosition(0), -1);
        QCOMPARE(controller.boxIndexForTextPosition(15), 1);
        QCOMPARE(controller.boxIndexForTextPosition(controller.resultText().length()), 1);

        controller.revertCurrentPageEdits();
        QVERIFY(controller.blockTextMapped());
        QCOMPARE(controller.blockTextRange(0), QList<int>({0, 18}));
        QCOMPARE(controller.blockTextRange(1), QList<int>({20, 17}));
    }

    // The model must refuse a verbatim copy of another page's text — that is
    // cross-page contamination (a stale panel), not a user edit.
    void setCurrentPageTextRefusesAnotherPagesText()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setAutoCheck(false);

        auto &controller = *m_controller;
        // The mock server answers every page with the same content, so both
        // pages end up holding identical recognized text.
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_pdf)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 4, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        controller.recognizeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), kImportTimeoutMs);
        QVERIFY2(controller.hasResult(), qPrintable(controller.statusMessage()));
        QCOMPARE(controller.resultText(), QStringLiteral("## 1. Introduction\n\nSecond block text"));

        // Identical to the neighbors: refused, the page keeps its edited text.
        controller.setCurrentPageText(QStringLiteral("## 1. Introduction\n\nSecond block text edited"));
        QCOMPARE(controller.resultText(), QStringLiteral("## 1. Introduction\n\nSecond block text edited"));  // unique text passes
        controller.setCurrentPageText(QStringLiteral("## 1. Introduction\n\nSecond block text"));
        QCOMPARE(controller.resultText(), QStringLiteral("## 1. Introduction\n\nSecond block text edited"));
        QCOMPARE(controller.currentPageEdited(), true);

        // Clearing the page is still allowed even though other pages exist.
        controller.setCurrentPageText(QString());
        QCOMPARE(controller.resultText(), QString());
    }

    // The plain-text panel paints the selected block's range through the
    // controller-owned highlighter; a manual edit clears the paint and Revert
    // brings it back.
    void blockHighlighterPaintsTheSelectedRange()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setAutoCheck(false);

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        controller.recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), kImportTimeoutMs);
        QVERIFY2(controller.hasResult(), qPrintable(controller.statusMessage()));

        QTextDocument document;
        controller.attachBlockTextHighlighter(&document, QColor(Qt::yellow));

        // Layout formats are where QSyntaxHighlighter lands its ranges.
        const auto paintedRanges = [](const QTextDocument &document) {
            QList<QPair<int, int>> out;
            int offset = 0;
            for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
                for (const QTextLayout::FormatRange &range : block.layout()->formats())
                    out.append({offset + range.start, range.length});
                offset += block.length();
            }
            return out;
        };

        // The test owns the document text (QML's TextArea normally does).
        document.setPlainText(controller.resultText());

        controller.setSelectedBoxIndex(0);
        QCOMPARE(paintedRanges(document), (QList<QPair<int, int>>({{0, 18}})));

        controller.setSelectedBoxIndex(1);
        QCOMPARE(paintedRanges(document), (QList<QPair<int, int>>({{20, 17}})));

        controller.setCurrentPageText(controller.resultText() + QStringLiteral(" appended"));
        // Best-effort re-anchoring: the selected block keeps its paint.
        QCOMPARE(paintedRanges(document), (QList<QPair<int, int>>({{20, 17}})));

        controller.revertCurrentPageEdits();
        // The selection still points at block 1; the paint comes back with it.
        QCOMPARE(paintedRanges(document), (QList<QPair<int, int>>({{20, 17}})));
    }

    // ADR 134: a block that already carries an answer is not asked again. Both
    // header buttons used to re-run the whole set, so a second «Check all» paid
    // for every block a second time and overwrote a stored FIX with a fresh
    // guess; the request count is the proof, since «nothing happened» and «it was
    // re-checked and agreed» look identical in the box model.
    void theQueueAsksOnlyForBlocksWithoutAnAnswer()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setCheckModelName(QStringLiteral("check-test"));
        m_settings->setAutoCheck(false);

        // A box needs at least a pixel in both directions to become a crop, and
        // the det-token boxes are normalized: on the shared 39x27 fixture the
        // title box is 0.4 px tall and the queue drops it before sending
        // anything. A page-sized raster keeps both boxes verifiable.
        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("verify-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        m_controller->recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), kImportTimeoutMs);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));
        QCOMPARE(server.requests(), 1);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);
        const int statusRole = BoxListModel::CheckStatusRole;
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::NotChecked));

        m_controller->checkEnabledBlocksOnPage();
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->checkProgressTotal(), 2, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 3);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Ok));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Ok));

        // Answered: neither entry point sends anything, and the status line says
        // why instead of leaving the button looking broken.
        m_controller->checkEnabledBlocksOnPage();
        QVERIFY(!m_controller->checkRunning());
        QVERIFY2(m_controller->statusMessage().contains(QStringLiteral("already verified")), qPrintable(m_controller->statusMessage()));
        m_controller->checkAllEnabledBlocks();
        QVERIFY(!m_controller->checkRunning());
        QCOMPARE(server.requests(), 3);

        // The per-block «Verify» button is the one way back in.
        m_controller->setSelectedBoxIndex(0);
        m_controller->checkSelectedBlock();
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 4, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);

        // A re-recognized block is asked again: the fresh OCR text has no answer.
        m_controller->recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), kImportTimeoutMs);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::NotChecked));
        m_controller->checkAllEnabledBlocks();
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->checkProgressTotal(), 2, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 7);
    }

    // The decision phase runs first for every queued block; with automatic
    // re-recognition on, the mismatches go straight to the block-recognition
    // model in a second phase of the same run, and the transcription becomes
    // the block text.
    void decisionMismatchFeedsRecheckWhenEnabled()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // below the 0.5 threshold: every block mismatches
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setCheckModelName(QStringLiteral("check-test"));
        m_settings->setAutoCheck(false);
        m_settings->setAutoRecheck(true);

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("recheck-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        m_controller->recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), kImportTimeoutMs);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;
        const int correctedRole = BoxListModel::CorrectedRole;

        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        // 1 recognition + 2 decision requests + 2 re-recognition requests.
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 5, kImportTimeoutMs);
        QCOMPARE(server.systemoneRequests(), 2);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Fixed));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Fixed));
        QCOMPARE(boxes->data(boxes->index(0), correctedRole).toString(), server.transcription);
        QCOMPARE(boxes->data(boxes->index(1), correctedRole).toString(), server.transcription);

        // Answered: a second run asks nothing.
        m_controller->checkEnabledBlocksOnPage();
        QVERIFY(!m_controller->checkRunning());
        QCOMPARE(server.requests(), 5);
    }

    // Without the re-recognition option the run ends after the decision phase:
    // the rejected blocks keep their red mark for the user to act on.
    void decisionMismatchStaysWithoutRecheck()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setCheckModelName(QStringLiteral("check-test"));
        m_settings->setAutoCheck(false);
        m_settings->setAutoRecheck(false);

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("norecheck-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        m_controller->recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), kImportTimeoutMs);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;

        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 3);  // 1 recognition + 2 decision, no phase 2
        QCOMPARE(server.systemoneRequests(), 2);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedRole).toString(), QString());
    }

    // The block-recognition entry points: the selected block is re-run through
    // the transcription model whatever its status, and the problem-block
    // commands pick only the Mismatch/Review marks. A block that is already
    // Fixed is not asked again.
    void recognizeBlockCommandsReRunOnlyTheMarkedBlocks()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // the check run marks everything Mismatch
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setCheckModelName(QStringLiteral("check-test"));
        m_settings->setAutoCheck(false);
        m_settings->setAutoRecheck(false);

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("recognize-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        m_controller->recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), kImportTimeoutMs);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;

        // The per-block button works on an unchecked block too.
        m_controller->setSelectedBoxIndex(0);
        m_controller->recognizeSelectedBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 2, kImportTimeoutMs);
        QCOMPARE(server.systemoneRequests(), 0);  // straight to the transcription stage
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Fixed));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedRole).toString(), server.transcription);

        // The check run asks only the block left unchecked (ADR 134), and it
        // mismatches — no automatic re-recognition.
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 3);
        QCOMPARE(server.systemoneRequests(), 1);
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Mismatch));

        // The problem-block command re-runs exactly the marked one.
        QVERIFY(m_controller->pageProblemRecognitionSupported());
        m_controller->recognizeProblemBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 4);
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Fixed));

        // Nothing is marked anymore: the command has nothing to do and asks
        // no server.
        QVERIFY(!m_controller->pageProblemRecognitionSupported());
        QVERIFY(!m_controller->allProblemRecognitionSupported());
        m_controller->recognizeAllProblemBlocks();
        QVERIFY(!m_controller->checkRunning());
        QCOMPARE(server.requests(), 4);
    }

    // A failed decision request is not a verdict: the block stays unchecked
    // (so a later run retries it) and the error is reported.
    void decisionErrorLeavesBlockUnchecked()
    {
        DetTokenChatServer server;
        server.systemoneBroken = true;  // the endpoint answers, but not with a decision
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setCheckModelName(QStringLiteral("check-test"));
        m_settings->setAutoCheck(false);

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("deverr-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        m_controller->recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), kImportTimeoutMs);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;

        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 3);  // both blocks were asked
        QCOMPARE(server.systemoneRequests(), 2);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QVERIFY2(m_controller->checkErrorMessage().contains(QStringLiteral("answers")), qPrintable(m_controller->checkErrorMessage()));
    }

    // Regression (ADR 102): the page owns its text, so re-typing what the page
    // shows cannot discard a structural edit. With the old two-place model the
    // comparison was made against the parse-time text, so typing the visible
    // text back deleted the edit-store entry — resurrecting the removed block
    // and losing the verified correction.
    void typingTheVisibleTextBackKeepsStructuralEdits()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setAutoCheck(false);

        m_controller->openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        m_controller->recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), kImportTimeoutMs);
        QVERIFY(m_controller->hasResult());

        QVERIFY(m_controller->removeBlock(1));
        const QString afterRemoval = m_controller->resultText();
        QVERIFY(!afterRemoval.contains(QStringLiteral("Second block text")));
        QVERIFY(m_controller->currentPageEdited());

        // Typing the very same text back is a no-op, not a revert.
        m_controller->setCurrentPageText(afterRemoval);
        QCOMPARE(m_controller->resultText(), afterRemoval);
        QVERIFY(m_controller->currentPageEdited());
        QVERIFY(!m_controller->resultText().contains(QStringLiteral("Second block text")));

        // A real user edit goes through; Revert rolls the text back to the last
        // structurally-consistent state — the removed block stays removed (its
        // box is gone, so its text must not come back), and the page can never
        // regress to a text/boxes mismatch.
        m_controller->setCurrentPageText(QStringLiteral("typed by hand"));
        QCOMPARE(m_controller->resultText(), QStringLiteral("typed by hand"));
        QVERIFY(m_controller->currentPageEdited());

        m_controller->revertCurrentPageEdits();
        QVERIFY(!m_controller->currentPageEdited());
        QVERIFY(m_controller->resultText().contains(QStringLiteral("1. Introduction")));
        QVERIFY(!m_controller->resultText().contains(QStringLiteral("Second block text")));

        // The structural edit is still reproducible afterwards: the text edits
        // and the revert did not touch the boxes, so the remaining block can be
        // deleted again (only the removed one is gone).
        QCOMPARE(qobject_cast<QAbstractItemModel *>(m_controller->boxModel())->rowCount(), 1);
        QVERIFY2(m_controller->removeBlock(0), qPrintable(QStringLiteral("pages=%2 hasResult=%3").arg(m_controller->pageCount()).arg(m_controller->hasResult())));
        QVERIFY(!m_controller->resultText().contains(QStringLiteral("1. Introduction")));
    }

    // The remove → Revert cycle used to consume the edit baseline: the first
    // Revert restored the pre-removal text (resurrecting removed blocks), the
    // second removal ran without a baseline and the next Revert wiped the page
    // text entirely. Structural changes now rebase the baseline, so Revert
    // only undoes manual text edits.
    void structuralEditsRebaseTheEditBaseline()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setAutoCheck(false);

        m_controller->openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        m_controller->recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), kImportTimeoutMs);
        QVERIFY(m_controller->hasResult());

        QVERIFY(m_controller->removeBlock(1));
        QCOMPARE(m_controller->resultText(), QStringLiteral("## 1. Introduction"));
        QVERIFY(m_controller->currentPageEdited());

        // Revert must not resurrect the removed block's text.
        m_controller->revertCurrentPageEdits();
        QCOMPARE(m_controller->resultText(), QStringLiteral("## 1. Introduction"));
        QCOMPARE(m_controller->currentPageEdited(), false);

        // A second removal + Revert cycle must not wipe the page either.
        m_controller->setSelectedBoxIndex(0);
        QVERIFY(m_controller->removeBlock(0));
        QCOMPARE(m_controller->resultText(), QString());
        m_controller->revertCurrentPageEdits();
        QCOMPARE(m_controller->resultText(), QString());
        QCOMPARE(m_controller->currentPageEdited(), false);
    }

    // The project file is the whole working state: pages (with their sources),
    // recognized blocks, verification results, manual edits and the current
    // page. Saving and reopening it must restore all of that and keep the
    // document fully functional (re-rendering, further saves).
    void projectSaveAndReopenRestoresTheWorkingState()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setAutoCheck(false);

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_pdf)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 4, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        controller.recognizeCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), kImportTimeoutMs);
        QVERIFY(controller.hasResult());
        QVERIFY(controller.removeBlock(1));
        QVERIFY(controller.currentPageEdited());
        controller.setCurrentPageText(controller.resultText() + QStringLiteral("\nhand-added line"));

        // A different page becomes current, and the pages are reordered.
        controller.setCurrentPage(2);
        QVERIFY(controller.movePage(0, 3));
        QCOMPARE(controller.currentPage(), 1);

        const QString projectPath = m_dir.filePath(QStringLiteral("saved.llocr"));
        QSignalSpy busySpy(&controller, &AppController::projectBusyChanged);
        controller.saveProject(QUrl::fromLocalFile(projectPath));
        QVERIFY(busySpy.count() >= 1);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.projectBusy(), kImportTimeoutMs);
        QCOMPARE(controller.projectPath().toLocalFile(), projectPath);
        QCOMPARE(controller.projectFileName(), QStringLiteral("saved.llocr"));
        QVERIFY(QFile::exists(projectPath));

        // Reopen into the same controller: the whole state must come back.
        controller.openProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.projectBusy(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 4);
        QCOMPARE(controller.currentPage(), 1);

        // Page 0 after the move is the former PDF page 1; page 3 is the
        // recognized raster with its edit.
        QVERIFY(!controller.currentPageEditable());
        controller.setCurrentPage(3);
        QVERIFY(controller.currentPageEditable());
        QVERIFY(controller.currentPageEdited());
        const QString restored = controller.resultText();
        QVERIFY(restored.contains(QStringLiteral("1. Introduction")));
        QVERIFY(restored.contains(QStringLiteral("hand-added line")));
        QVERIFY(!restored.contains(QStringLiteral("Second block text")));  // the removed block stays removed
        QCOMPARE(qobject_cast<QAbstractItemModel *>(controller.boxModel())->rowCount(), 1);

        // Revert undoes the manual text edit and lands on the last
        // structurally-consistent state — the removed block stays removed.
        controller.revertCurrentPageEdits();
        QVERIFY(!controller.currentPageEdited());
        QVERIFY(!controller.resultText().contains(QStringLiteral("Second block text")));
        QVERIFY(!controller.resultText().contains(QStringLiteral("hand-added line")));

        // The embedded sources still render: the raster page keeps its colour.
        const QImage rasterPage = controller.pageImage(3);
        compareQuadrants(rasterPage.scaled(32, 32, Qt::KeepAspectRatio), {Qt::yellow, Qt::yellow, Qt::yellow, Qt::yellow});

        // Saving the reopened project works again (sources come from the
        // session directory), and a second open replaces the session.
        const QString secondPath = m_dir.filePath(QStringLiteral("again.llocr"));
        controller.saveProject(QUrl::fromLocalFile(secondPath));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.projectBusy(), kImportTimeoutMs);
        controller.openProject(QUrl::fromLocalFile(secondPath));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.projectBusy(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 4);
        QCOMPARE(controller.projectPath().toLocalFile(), secondPath);
    }

    void failedImportsClearStateAndAllowRetry_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("unreadable") << QStringLiteral("unreadable");
        QTest::newRow("missing") << QStringLiteral("missing");
    }

    void failedImportsClearStateAndAllowRetry()
    {
        QFETCH(QString, kind);
        const QString path = kind == QStringLiteral("unreadable") ? m_broken : m_dir.filePath(QStringLiteral("missing.pdf"));
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

        controller.openFiles({QUrl::fromLocalFile(m_pdf)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 3);
        QCOMPARE(observations.importingStates, QList<bool>({true, false, true, false}));
        compareQuadrants(controller.pageImage(2), colors(2));

        controller.setCurrentPage(1);
        const qint64 imageKey = controller.pageImage(controller.currentPage()).cacheKey();
        const qint64 thumbKey = controller.pageThumbnail(1).cacheKey();
        observations.committedCounts.clear();
        controller.openFiles({QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 3);
        QCOMPARE(qobject_cast<PageListModel *>(controller.pageModel())->rowCount(), 3);
        QCOMPARE(controller.currentPage(), 1);
        QCOMPARE(controller.pageImage(controller.currentPage()).cacheKey(), imageKey);
        QCOMPARE(controller.pageThumbnail(1).cacheKey(), thumbKey);
        QVERIFY(controller.currentPageWarning().isEmpty());
        QVERIFY(observations.committedCounts.isEmpty());
        QVERIFY(!observations.wrongThread.load());
        QVERIFY2(controller.statusMessage().contains(path), qPrintable(controller.statusMessage()));
        for (int row = 0; row < 3; ++row)
            compareQuadrants(controller.pageImage(row), colors(row));
    }

#ifdef LLOCR_HAVE_DJVU
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
        m_controller->openFiles({QUrl::fromLocalFile(m_multipage), QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_multipage)});
        QVERIFY(m_controller->importing());
        m_controller.reset();
        QVERIFY(weak.isNull());
        if (gate)
            gate->release();
        QVERIFY2(QThreadPool::globalInstance()->waitForDone(kImportTimeoutMs), "Detached import worker did not finish");
        // Dispatch stale watcher events/queued next-file callbacks, if any.
        QTest::qWait(20);
        QCOMPARE(observations.importingStates, QList<bool>({true}));
        QVERIFY(observations.committedCounts.isEmpty());
        QVERIFY(!observations.wrongThread.load());
        m_controller = std::make_unique<AppController>(*m_settings, *m_runtime, *m_requestProfiles, *m_checkRequestProfiles, *m_verification);
        m_controller->openFiles({QUrl::fromLocalFile(m_multipage)});
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        QCOMPARE(m_controller->pageCount(), 3);
        compareQuadrants(m_controller->pageImage(1), colors(1));
    }
#endif  // LLOCR_HAVE_DJVU (asynchronous import path)
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
