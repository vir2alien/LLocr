#include <QtTest>

#include <atomic>
#include <cmath>
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
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
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
#include <QTransform>
#include <set>

#include "app/AppController.h"
#include "app/BoxListModel.h"
#include "app/PageListModel.h"
#include "app/PageRotation.h"
#include "app/ProblemLog.h"
#include "app/UiController.h"
#include "app/VerificationPromptStore.h"
#include "app/VerificationQueueController.h"
#include "config/RequestProfileStore.h"
#include "core/ModelProfiles.h"
#include "core/StatusMessage.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/ZipWriter.h"
#include "testsettings.h"
#include "textpage.h"

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

QQuickItem *findFirstTextItem(QQuickItem *root)
{
    const QByteArray className = root->metaObject()->className();
    if (className.contains("TextArea") || className.contains("TextEdit"))
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = findFirstTextItem(child))
            return found;
    }
    return nullptr;
}

QQuickItem *findButtonWithText(QQuickItem *root, const QString &text)
{
    if (QByteArray(root->metaObject()->className()).contains("Button") && root->property("text").toString() == text)
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = findButtonWithText(child, text))
            return found;
    }
    return nullptr;
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
// fixed reply, so the real recognition pipeline produces real blocks.
// Used to drive the AppController-level block-removal contract end to end.
// It speaks every protocol the app sends: the layout markup request (its
// prompt carries "Layout Detection:"), the block-recognition transcription
// (identified by its system message), the decision model's /v1/systemone
// (probability is configurable — below the 0.5 default threshold every block
// mismatches), and the fix protocol (a request carrying a <CANDIDATE>
// payload — the reply is the configurable OK / FIX / REVIEW answer).
// Request counting is what makes "this block was not asked again" observable.
class DetTokenChatServer : public QObject
{
public:
    explicit DetTokenChatServer(QObject *parent = nullptr) : QObject(parent) { connect(&m_server, &QTcpServer::newConnection, this, &DetTokenChatServer::onNewConnection); }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    int requests() const { return m_requests; }

    int systemoneRequests() const { return m_systemoneRequests; }

    // The raw request buffers of every chat-completions call, in arrival
    // order — what the recheck framing assertions read.
    const QList<QByteArray> &chatRequests() const { return m_chatRequests; }

    // The same for the /v1/systemone calls — what the judged `state` text
    // (the decision candidate) reads.
    const QList<QByteArray> &systemoneBodies() const { return m_systemoneBodies; }

    qreal systemoneProbability = 0.97;
    bool systemoneBroken = false;  // reply without an "answers" object → every judge fails
    QString transcription = QStringLiteral("Re-recognized block text");
    // The transcription answers, taken in request order and repeated from the
    // start when the list runs out; an empty list answers every block with
    // `transcription`. The default pair reproduces the page the single-pass
    // OCR used to produce: a title block and a text block.
    QStringList transcriptionSequence = {QStringLiteral("1. Introduction"), QStringLiteral("Second block text")};
    // The fix protocol's reply for any request carrying a <CANDIDATE>. The
    // reply carries the complete corrected block and nothing else;
    // fixReplies overrides per candidate.
    QString fixReply = QStringLiteral("FIX\nFixed block text");
    QHash<QString, QString> fixReplies;
    // The layout reply (the "\nLayout Detection:" prompt): the markup-shaped
    // block lines the layout parser reads. The default pair mirrors the
    // transcription sequence: a title block over a text block, both large
    // enough to crop on the smallest fixture.
    QString layoutContent = QStringLiteral("<|box_start|>80 60 900 160<|box_end|><|ref_start|>title<|ref_end|>\n\n"
                                           "<|box_start|>80 200 900 800<|box_end|><|ref_start|>text<|ref_end|>");

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
                if (m_buffers[socket].contains("GET /v1/models")) {
                    // The app asks the server for the models' context window
                    // (ADR 231). The probe is infrastructure, not a task
                    // request: it is neither counted nor listed.
                    socket->write(modelsReply());
                    socket->flush();
                    socket->disconnectFromHost();
                    m_buffers.remove(socket);
                    return;
                }
                ++m_requests;
                if (!m_buffers[socket].contains("POST /v1/systemone"))
                    m_chatRequests.append(m_buffers[socket]);
                else
                    m_systemoneBodies.append(m_buffers[socket]);
                socket->write(reply(m_buffers[socket]));
                socket->flush();
                socket->disconnectFromHost();
                m_buffers.remove(socket);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    // The candidate text between the <CANDIDATE> markers; the wire is JSON,
    // so the newlines around it are the two-character `\n` sequences.
    static QString candidateOf(const QByteArray &request)
    {
        const QByteArray begin = "<CANDIDATE>\\n";
        const QByteArray end = "\\n</CANDIDATE>";
        const int at = request.indexOf(begin);
        if (at < 0)
            return {};
        const int from = at + begin.size();
        const int to = request.indexOf(end, from);
        return to < 0 ? QString() : QString::fromUtf8(request.mid(from, to - from));
    }

    QString fixAnswer(const QByteArray &request) const
    {
        const QString candidate = candidateOf(request);
        const auto overridden = fixReplies.constFind(candidate);
        if (overridden != fixReplies.constEnd())
            return overridden.value();
        return QString(fixReply).replace(QStringLiteral("%1"), candidate);
    }

    QString nextTranscription() const
    {
        if (transcriptionSequence.isEmpty())
            return transcription;
        const QString text = transcriptionSequence.at(const_cast<DetTokenChatServer *>(this)->m_transcriptionPos++ % transcriptionSequence.size());
        return text;
    }

    // The server's model list: the context window of every model (ADR 231).
    static QByteArray modelsReply()
    {
        const QByteArray body =
            QJsonDocument(
                QJsonObject{{"object", "list"}, {"data", QJsonArray{QJsonObject{{"id", "det-token-test"}, {"max_model_len", 16384}}, QJsonObject{{"id", "check-test"}, {"max_model_len", 16384}}}}})
                .toJson(QJsonDocument::Compact);
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
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
            // Chat completions: the layout markup for the markup prompt, the
            // fix protocol answer for a candidate-carrying request, the block
            // transcription otherwise; the model streams newlines as the two
            // characters `\n`.
            const QString content = request.contains("<CANDIDATE>") ? fixAnswer(request) : request.contains("Layout Detection:") && !layoutContent.isEmpty() ? layoutContent : nextTranscription();
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
    QList<QByteArray> m_chatRequests;
    QList<QByteArray> m_systemoneBodies;
    int m_requests = 0;
    int m_systemoneRequests = 0;
    int m_transcriptionPos = 0;
};

// The single-pass page OCR is gone: a page becomes recognized through the
// layout pass (the markup the mock server answers with) and then the block
// transcription of the text-less blocks — the two-step path the «Разметка»
// toolbar group drives. The scope is the thumbnail selection, so an empty
// selection recognizes every page.
void recognizePage(AppController &controller)
{
    controller.layoutScopePages();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.layoutBusy(), kImportTimeoutMs);
    controller.recognizeScopeBlocks();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), kImportTimeoutMs);
}

class TestAppImport : public QObject
{
    Q_OBJECT

private:
    TestSettingsIsolation m_settingsIsolation;
    QTemporaryDir m_dir;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<LaunchProfileStore> m_launchProfiles;
    std::unique_ptr<RequestProfileStore> m_checkRequestProfiles;
    std::unique_ptr<RequestProfileStore> m_layoutRequestProfiles;
    std::unique_ptr<RequestProfileStore> m_fixRequestProfiles;
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
        // The orientation check reads the picture alone; the tests about the
        // check itself turn it on.
        m_settings->setOrientationCheckEnabled(false);
        // A valid model makes the recognition guard meaningful; an unsupported
        // local URL prevents any network traffic even if that guard regresses.
        m_settings->setModelName(QStringLiteral("import-guard-test"));
        m_settings->setBaseUrl(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("no-server"))).toString());
        m_launchProfiles = std::make_unique<LaunchProfileStore>(*m_settings);
        m_checkRequestProfiles = std::make_unique<RequestProfileStore>(*m_settings, ConnectionRole::BlockRecognition);
        m_layoutRequestProfiles = std::make_unique<RequestProfileStore>(*m_settings, ConnectionRole::Layout);
        m_fixRequestProfiles = std::make_unique<RequestProfileStore>(*m_settings, ConnectionRole::Fix);
        m_verification = std::make_unique<VerificationPromptStore>(*m_settings);
        m_runtime = std::make_unique<RuntimeController>(*m_settings, *m_launchProfiles);
        m_problems = std::make_unique<ProblemLog>();
        m_controller = std::make_unique<AppController>(*m_settings, *m_runtime, *m_checkRequestProfiles, *m_layoutRequestProfiles, *m_fixRequestProfiles, *m_verification);
        m_controller->setProblemLog(m_problems.get());
        // The decision is the visual diff (docs/31): the crop goes out next to
        // the candidate rendered back into an image. Chromium cannot
        // initialize in this suite, so the render leg is a stand-in painter;
        // the shipped profile, the wire format and the routing stay real. The
        // decision task points at the shipped default (minicpm-jev).
        m_controller->setCandidateRenderFunction([](const QString &candidateText, const std::function<void(bool, const QImage &, const QString &)> &done) {
            Q_UNUSED(candidateText);
            QImage image(64, 32, QImage::Format_ARGB32);
            image.fill(Qt::white);
            done(true, image, {});
        });

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
        m_launchProfiles.reset();
        m_settings.reset();
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

    // A project whose embedded files are all missing leaves the document
    // empty; the page index must stay at 0 instead of being clamped to -1,
    // which is undefined behaviour (ADR 199).
    void projectWithoutEmbeddedFilesOpensToPageZero()
    {
        auto &controller = *m_controller;
        const QString projectPath = m_dir.filePath(QStringLiteral("empty.llocr"));
        {
            ZipWriter zip(projectPath);
            QVERIFY(zip.addEntry(QStringLiteral("project.json"),
                                 QByteArrayLiteral("{\"format\":\"llocr-project\",\"version\":2,\"currentPage\":3,"
                                                   "\"sources\":[{\"id\":0,\"file\":\"sources/0_missing.png\",\"name\":\"missing.png\",\"type\":\"image\"}],"
                                                   "\"pages\":[{\"sourceId\":0,\"sourcePageIndex\":0}]}")));
            QVERIFY(zip.finish());
        }

        controller.openProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.projectBusy(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 0);
        QCOMPARE(controller.currentPage(), 0);
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
        ImportSignals observations(controller);
        QSignalSpy busySpy(&controller, &AppController::busyChanged);
        QSignalSpy runtimeBusySpy(m_runtime.get(), &RuntimeController::busyStateChanged);

        ImportWorkerGate gate;
        QVERIFY(gate.waitUntilHeld());
        controller.openFiles({QUrl::fromLocalFile(m_multipage)});
        QVERIFY(controller.importing());
        QVERIFY(!controller.busy());
        const QString status = controller.statusMessage();
        for (int attempt = 0; attempt < 3; ++attempt) {
            controller.openFiles({QUrl::fromLocalFile(m_raster)});
            controller.layoutScopePages();
            QVERIFY(!controller.addBlock(QStringLiteral("text")));
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
        QVERIFY(controller.movePage(2, 0));
        compareQuadrants(controller.pageImage(0), colors(0));
        QVERIFY(controller.removePage(0));
        QCOMPARE(controller.pageCount(), 4);
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 5);
    }

    void theAvailabilitySignalFollowsTheFlags()
    {
        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 2);
        // The current page is selected by default.
        QCOMPARE(controller.selectedPageCount(), 1);

        QSignalSpy spy(&controller, &AppController::availabilityChanged);

        // A page change that moves no flag announces nothing: the pages are
        // alike, so the availability of the document did not change.
        controller.setCurrentPage(1);
        QCOMPARE(spy.count(), 0);

        // The selection is a flag of its own: one announcement, not one per
        // trigger.
        auto *pages = qobject_cast<PageListModel *>(controller.pageModel());
        QVERIFY(pages);
        pages->clearSelection();
        QCOMPARE(controller.selectedPageCount(), 0);
        QCOMPARE(spy.count(), 1);
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
        QSignalSpy renderingSpy(&controller, &AppController::previewRenderingChanged);
        // The first call cannot have a full image yet — that is the point.
        QVERIFY(controller.previewImage(0).isNull());
        QVERIFY(controller.previewRendering(0));
        QCOMPARE(controller.previewRenderingPage(), 0);

        QTRY_VERIFY_WITH_TIMEOUT(!controller.previewRendering(0), 20000);
        QCOMPARE(controller.previewRenderingPage(), -1);
        QCOMPARE(renderingSpy.count(), 2);  // start + finish, both notify the QML binding
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

        m_controller->openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        // Recognition is refused while an import is still running.
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        recognizePage(*m_controller);
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
        // A removed block is a structural change, not a manual edit: the text
        // still follows the blocks, so the pipeline may write it again.
        QVERIFY(!m_controller->currentPageEdited());

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

    // The operator's flow on a page that carries markup as well: a block is
    // removed, a new one is drawn over the table area and recognized — the new
    // block's text must reach the page text (the MD view and the preview).
    void drawnBlockReachesThePageTextOnAMarkupPage()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        server.transcriptionSequence.clear();  // every block reads `transcription`

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);
        QVERIFY(controller.hasResult());

        auto *boxes = qobject_cast<BoxListModel *>(controller.boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);

        // The page carries markup too: a drawn table block with no text.
        QVERIFY(controller.addBlockAt(QStringLiteral("table"), 0.1, 0.5, 0.3, 0.2));
        QCOMPARE(boxes->rowCount(), 3);

        // The operator deletes a block…
        QVERIFY(controller.removeBlock(1));
        QCOMPARE(boxes->rowCount(), 2);

        // …draws a new one over the table area…
        QVERIFY(controller.addBlockAt(QStringLiteral("table"), 0.5, 0.5, 0.3, 0.2));
        QCOMPARE(boxes->rowCount(), 3);
        QCOMPARE(controller.selectedBoxIndex(), 2);

        // …and recognizes it.
        const int requests = server.requests();
        controller.recognizeSelectedBlock();
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), requests + 1, kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(controller.selectedBlockText(), server.transcription, kImportTimeoutMs);

        // The page text follows the blocks: the new block's content is in the
        // document.
        QVERIFY2(controller.resultText().contains(server.transcription), qPrintable(controller.resultText()));
        // And nothing freezes it: the removal is not a manual edit.
        QVERIFY(!controller.currentPageEdited());
    }

    // A removal changes the blocks, not the text the user wrote by hand.
    void removalKeepsTheUsersOwnText()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);
        QVERIFY(controller.hasResult());

        controller.setCurrentPageText(controller.resultText() + QStringLiteral("\ntyped by hand"));
        QVERIFY(controller.currentPageEdited());

        QVERIFY(controller.removeBlock(1));

        // The user's text survives the removal…
        QVERIFY2(controller.resultText().contains(QStringLiteral("typed by hand")), qPrintable(controller.resultText()));
        // …and the page is still the user's own text.
        QVERIFY(controller.currentPageEdited());
    }

    // The pipeline's new content lands in a page the user edited by hand: the
    // block the operator just made must be in the document, and the user's own
    // text must survive.
    void pipelineContentLandsInAHandEditedPage()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        server.transcriptionSequence.clear();  // every block reads `transcription`

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);
        QVERIFY(controller.hasResult());

        // The operator edits the page text by hand…
        controller.setCurrentPageText(controller.resultText() + QStringLiteral("\ntyped by hand"));
        QVERIFY(controller.currentPageEdited());

        // …draws a block and recognizes it.
        QVERIFY(controller.addBlockAt(QStringLiteral("text"), 0.1, 0.6, 0.3, 0.1));
        const int requests = server.requests();
        controller.recognizeSelectedBlock();
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), requests + 1, kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(controller.selectedBlockText(), server.transcription, kImportTimeoutMs);

        // The new block's content is in the document…
        QVERIFY2(controller.resultText().contains(server.transcription), qPrintable(controller.resultText()));
        // …and the user's own text is still there.
        QVERIFY2(controller.resultText().contains(QStringLiteral("typed by hand")), qPrintable(controller.resultText()));
    }

    // The operator's flow: a block is removed, a new one is drawn and
    // recognized — the new block's text must reach the view model (the
    // preview) and the page text (the MD view).
    void drawnBlockReachesThePageTextAfterARemoval()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        server.transcriptionSequence.clear();  // every block reads `transcription`

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);
        QVERIFY(controller.hasResult());

        auto *boxes = qobject_cast<BoxListModel *>(controller.boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);

        // The operator deletes a block…
        QVERIFY(controller.removeBlock(1));
        QCOMPARE(boxes->rowCount(), 1);

        // …draws a new one…
        QVERIFY(controller.addBlockAt(QStringLiteral("text"), 0.1, 0.1, 0.3, 0.1));
        QCOMPARE(boxes->rowCount(), 2);
        QCOMPARE(controller.selectedBoxIndex(), 1);

        // …and recognizes it.
        const int requests = server.requests();
        controller.recognizeSelectedBlock();
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), requests + 1, kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(boxes->data(boxes->index(1), BoxListModel::TextRole).toString(), server.transcription, kImportTimeoutMs);

        // The block carries the text…
        QCOMPARE(controller.selectedBlockText(), server.transcription);
        // …and the page text follows it.
        QVERIFY2(controller.resultText().contains(server.transcription), qPrintable(controller.resultText()));
    }

    // A manually added block: an empty block of the given type lands on the
    // current page, is selected and the page counts as edited. The page text
    // follows the blocks only while it is the block-derived text.
    void addedBlockLandsOnTheCurrentPage()
    {
        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        auto *boxes = qobject_cast<BoxListModel *>(controller.boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 0);

        QVERIFY(controller.addBlock(QStringLiteral("table")));
        QCOMPARE(boxes->rowCount(), 1);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::LabelRole).toString(), QStringLiteral("table"));
        QCOMPARE(controller.selectedBoxIndex(), 0);
        // The page text is not a manual edit: it keeps following the blocks, so
        // the recognition of the new block reaches the text panel.
        QVERIFY(!controller.currentPageEdited());
        // An empty text block adds nothing to the page text.
        QVERIFY(controller.resultText().isEmpty());

        // An image block brings its placeholder with it.
        QVERIFY(controller.addBlock(QStringLiteral("image")));
        QCOMPARE(boxes->rowCount(), 2);
        QCOMPARE(controller.selectedBoxIndex(), 1);
        QVERIFY2(controller.resultText().contains(QStringLiteral("image://ocr/crop/")), qPrintable(controller.resultText()));
    }

    // A block the user drew on the page: it lands with the rectangle the
    // mouse drew, and the rectangle stays inside the page (ADR 189).
    void drawnBlockLandsWithItsRectangle()
    {
        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        auto *boxes = qobject_cast<BoxListModel *>(controller.boxModel());
        QVERIFY(boxes);

        QVERIFY(controller.addBlockAt(QStringLiteral("text"), 0.2, 0.3, 0.25, 0.1));
        QCOMPARE(boxes->rowCount(), 1);
        QCOMPARE(controller.selectedBoxIndex(), 0);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::XRole).toDouble(), 0.2);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::YRole).toDouble(), 0.3);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::WidthRole).toDouble(), 0.25);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::HeightRole).toDouble(), 0.1);

        // A rectangle that reaches past the page edge is pulled back into it.
        QVERIFY(controller.addBlockAt(QStringLiteral("text"), 0.9, 0.95, 0.3, 0.2));
        QCOMPARE(boxes->rowCount(), 2);
        const double x = boxes->data(boxes->index(1), BoxListModel::XRole).toDouble();
        const double y = boxes->data(boxes->index(1), BoxListModel::YRole).toDouble();
        const double w = boxes->data(boxes->index(1), BoxListModel::WidthRole).toDouble();
        const double h = boxes->data(boxes->index(1), BoxListModel::HeightRole).toDouble();
        QVERIFY(x >= 0.0 && x + w <= 1.0 + 1e-9);
        QVERIFY(y >= 0.0 && y + h <= 1.0 + 1e-9);
        QVERIFY(qAbs(x + w - 1.0) < 1e-9);
        QVERIFY(qAbs(y + h - 1.0) < 1e-9);
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

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        recognizePage(controller);
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

    // The rebuild memo must follow its key: an unrecognized page cannot serve
    // the previous page's cached mapping, and a parser-option change drops the
    // memo and re-announces the ranges.
    void blockTextMemoFollowsThePageAndParserOptions()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_pdf)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 4, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        recognizePage(controller);
        QVERIFY2(controller.hasResult(), qPrintable(controller.statusMessage()));
        QVERIFY(controller.blockTextMapped());

        controller.setCurrentPage(1);
        QVERIFY(!controller.blockTextMapped());

        controller.setCurrentPage(0);
        QVERIFY(controller.blockTextMapped());
        QCOMPARE(controller.blockTextRange(0), QList<int>({0, 18}));

        QSignalSpy rangeSpy(&controller, &AppController::blockTextRangeChanged);
        m_settings->setTablesAsHtml(!m_settings->tablesAsHtml());
        QCOMPARE(rangeSpy.count(), 1);
        QVERIFY(controller.blockTextMapped());
    }

    // The model must refuse a verbatim copy of another page's text — that is
    // cross-page contamination (a stale panel), not a user edit.
    void setCurrentPageTextRefusesAnotherPagesText()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));

        auto &controller = *m_controller;
        // The mock server answers every page with the same content, so both
        // pages end up holding identical recognized text.
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_pdf)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 4, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        qobject_cast<PageListModel *>(controller.pageModel())->clearSelection();  // empty selection = every page
        recognizePage(controller);
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

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        recognizePage(controller);
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
        m_settings->setModelName(QStringLiteral("check-test"));

        // A box needs at least a pixel in both directions to become a crop, and
        // the layout boxes are normalized: on the shared 39x27 fixture a short
        // box would be dropped by the queue before sending anything. A
        // page-sized raster keeps both boxes verifiable.
        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("verify-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));
        QCOMPARE(server.requests(), 3);  // 1 layout + 2 transcriptions

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);
        const int statusRole = BoxListModel::CheckStatusRole;
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::NotChecked));

        m_controller->checkEnabledBlocksOnPage();
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->checkProgressTotal(), 2, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 5);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Ok));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Ok));

        // Answered: neither entry point sends anything, and the status line says
        // why instead of leaving the button looking broken.
        m_controller->checkEnabledBlocksOnPage();
        QVERIFY(!m_controller->checkRunning());
        QVERIFY2(m_controller->statusMessage().contains(QStringLiteral("already verified")), qPrintable(m_controller->statusMessage()));
        m_controller->checkAllEnabledBlocks();
        QVERIFY(!m_controller->checkRunning());
        QCOMPARE(server.requests(), 5);

        // The per-block «Verify» button is the one way back in.
        m_controller->setSelectedBoxIndex(0);
        m_controller->checkSelectedBlock();
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 6, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);

        // A re-recognized block is asked again: the fresh OCR text has no answer.
        recognizePage(*m_controller);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::NotChecked));
        m_controller->checkAllEnabledBlocks();
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->checkProgressTotal(), 2, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 11);
    }

    // The queue holds (page, box) pairs captured at enqueue time, so a page or
    // block removed mid-run would shift every pending index: structural edits
    // are refused until the run is over (the header Stop is the way out), and
    // go through again once the queue has drained.
    void checkRunGuardsStructuralEdits()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_pdf)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 4, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        qobject_cast<PageListModel *>(controller.pageModel())->clearSelection();  // empty selection = every page
        recognizePage(controller);
        QVERIFY2(controller.hasResult(), qPrintable(controller.statusMessage()));

        controller.checkAllEnabledBlocks();
        QVERIFY(controller.checkRunning());
        QVERIFY(!controller.removePage(0));
        QVERIFY(!controller.movePage(0, 1));
        QVERIFY(!controller.removeBlock(0));
        QCOMPARE(controller.pageCount(), 4);
        // Block recognition and the markup pass are mutually exclusive with the
        // queue as well: the header/page-toolbar buttons and the C++ guards
        // agree.
        QSignalSpy busySpy(&controller, &AppController::busyChanged);
        controller.recognizeScopeBlocks();
        controller.layoutScopePages();
        QVERIFY(controller.busy());  // the queue itself is the running job
        QCOMPARE(busySpy.count(), 0);

        QTRY_VERIFY_WITH_TIMEOUT(!controller.checkRunning(), kImportTimeoutMs);
        QVERIFY(controller.movePage(2, 0));
        QVERIFY(controller.removePage(0));
        QCOMPARE(controller.pageCount(), 3);
    }

    // The decision phase judges every queued block; the mismatches stay marked
    // for the user. The fix is a separate action (ADR 178): the «Fix» button
    // sends the problem blocks through the fix protocol, and the answer lands
    // on the correction layer (ADR 164).
    void decisionMismatchIsFixedByTheSeparateFixRun()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // below the 0.5 threshold: every block mismatches
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));
        m_settings->setFixModelName(QStringLiteral("fix-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("recheck-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;
        const int textRole = BoxListModel::TextRole;
        const int correctedRole = BoxListModel::CorrectedRole;
        const int correctedStatusRole = BoxListModel::CorrectedCheckStatusRole;

        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        // 1 layout + 2 transcriptions + 2 decision requests; the fix stage did not run.
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 5, kImportTimeoutMs);
        QCOMPARE(server.systemoneRequests(), 2);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Mismatch));

        // The «Fix» action sends the problem blocks to the fix model.
        m_controller->fixAllProblemBlocks();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 7, kImportTimeoutMs);
        // The fix lands on the correction layer: the fixed text with no
        // verdict yet (the gray dot), while the text layer keeps its Mismatch.
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        // The fix never touches the text layer: the OCR text stays.
        QCOMPARE(boxes->data(boxes->index(0), textRole).toString(), QStringLiteral("1. Introduction"));
        QCOMPARE(boxes->data(boxes->index(1), textRole).toString(), QStringLiteral("Second block text"));
        QCOMPARE(boxes->data(boxes->index(0), correctedRole).toString(), QStringLiteral("Fixed block text"));
        QCOMPARE(boxes->data(boxes->index(0), correctedStatusRole).toInt(), int(BoxCheckStatus::NotChecked));

        // The fix request speaks the correction protocol (ADR 163): the
        // block's effective text travels as the <CANDIDATE> payload. The chat
        // log opens with the layout call, then the two transcriptions.
        QCOMPARE(server.chatRequests().size(), 5);
        QVERIFY(server.chatRequests().at(0).contains("Layout Detection:"));  // the markup pass
        QVERIFY(server.chatRequests().at(1).contains("\"role\":\"system\""));
        QVERIFY(!server.chatRequests().at(1).contains("<CANDIDATE>"));
        QVERIFY(server.chatRequests().at(3).contains("<CANDIDATE>"));
        QVERIFY(server.chatRequests().at(3).contains("1. Introduction"));
        QVERIFY(server.chatRequests().at(4).contains("<CANDIDATE>"));
        QVERIFY(server.chatRequests().at(4).contains("Second block text"));

        // Fixing an answered block re-runs the correction on the effective
        // text — the stored correction — still as a structured candidate. The
        // reply is the complete corrected block, so it replaces the whole
        // correction.
        server.fixReplies.insert(QStringLiteral("Fixed block text"), QStringLiteral("FIX\nFixed block text and more"));
        m_controller->setSelectedBoxIndex(0);
        m_controller->fixSelectedBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 8, kImportTimeoutMs);
        QCOMPARE(server.chatRequests().size(), 6);
        QVERIFY(server.chatRequests().at(5).contains("<CANDIDATE>"));
        QVERIFY(server.chatRequests().at(5).contains("Fixed block text"));
        QCOMPARE(boxes->data(boxes->index(0), correctedRole).toString(), QStringLiteral("Fixed block text and more"));

        // The next verification run judges the corrected text (the decision
        // request's state). Still below the threshold, so both mismatch again.
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 10, kImportTimeoutMs);  // +2 decision
        QCOMPARE(server.systemoneRequests(), 4);
        QVERIFY(server.systemoneBodies().at(2).contains("Fixed block text"));
        QVERIFY(!server.systemoneBodies().at(2).contains("1. Introduction"));
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        // The decision verdict rides on the correction layer, so the text
        // layer's confidence stayed where the first run left it.
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::ConfidenceRole).toDouble(), 0.1);
    }

    // The check run ends after the decision phase: the rejected blocks keep
    // their red mark for the user to act on.
    void decisionMismatchStaysUnfixed()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("norecheck-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;

        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 5);  // 1 layout + 2 transcriptions + 2 decision, no phase 2
        QCOMPARE(server.systemoneRequests(), 2);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedRole).toString(), QString());
        // The decision probability travels with the verdict for the
        // confidence badge.
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::ConfidenceRole).toDouble(), 0.1);
        QCOMPARE(boxes->data(boxes->index(1), BoxListModel::ConfidenceRole).toDouble(), 0.1);
    }

    // A formula lands in the canonical LaTeX form right after the
    // recognition (ADR 194), and the wire carries the block's own text: the
    // checker and the fixer see the same bytes the panel shows.
    void theFormulaLandsCanonicalAndTheWireCarriesIt()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.97;
        server.layoutContent = QStringLiteral("<|box_start|>112 132 884 309<|box_end|><|ref_start|>equation<|ref_end|>");
        server.transcriptionSequence = {QStringLiteral("$$Bi _ {1}$$")};
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("normformula-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);

        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.systemoneRequests(), 1);

        // The block carries the canonical form: the space before the brace is
        // gone ("Bi _ {1}" -> "Bi _{1}").
        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const QString formula = boxes->data(boxes->index(0), BoxListModel::TextRole).toString();
        QVERIFY2(formula.contains(QStringLiteral("Bi _{1}")), qPrintable(formula));
        QVERIFY2(!formula.contains(QStringLiteral("Bi _ {1}")), qPrintable(formula));

        // The wire carries the block's own text.
        const QByteArray body = server.systemoneBodies().first();
        QVERIFY2(body.contains("Bi _{1}"), body.constData());
        QVERIFY2(!body.contains("Bi _ {1}"), body.constData());
    }

    // A formula whose LaTeX is broken is rejected right after the recognition
    // (ADR 194): the block is red, no model is asked about it, and the
    // formula still stands in the document — the operator has to look at it.
    void aBrokenFormulaIsRejectedAndNotChecked()
    {
        DetTokenChatServer server;
        server.layoutContent = QStringLiteral("<|box_start|>112 132 884 309<|box_end|><|ref_start|>equation<|ref_end|>");
        server.transcriptionSequence = {QStringLiteral("$$\\frac{1}$$")};
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("brokenformula-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::InvalidSyntax));

        // The verification queue skips the rejected formula: no model is
        // asked about a text that cannot be rendered.
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.systemoneRequests(), 0);

        // The formula itself still stands in the document.
        QVERIFY(m_controller->resultText().contains(QStringLiteral("\\frac{1}")));
    }

    // The model answers a formula in a markdown code fence — sometimes inside
    // the math wrapper. The fence is carrier markup: it is unwrapped when the
    // formula lands, and the formula itself is sound (ADR 195).
    void aFormulaInAMarkdownFenceLandsWithoutTheFence()
    {
        DetTokenChatServer server;
        server.layoutContent = QStringLiteral("<|box_start|>112 132 884 309<|box_end|><|ref_start|>equation<|ref_end|>");
        server.transcriptionSequence = {QStringLiteral("$$\n```latex\nK_{\\ell} = \\frac{1}{R^*}\n```\n$$")};
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("fencedformula-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const QString formula = boxes->data(boxes->index(0), BoxListModel::TextRole).toString();
        QVERIFY2(!formula.contains(QStringLiteral("```")), qPrintable(formula));
        QVERIFY2(formula.contains(QStringLiteral("K_{\\ell}")), qPrintable(formula));

        // The page text follows the blocks: the fence does not reach the
        // document either.
        const QString pageText = m_controller->resultText();
        QVERIFY2(!pageText.contains(QStringLiteral("```")), qPrintable(pageText));
        QVERIFY2(pageText.contains(QStringLiteral("K_{\\ell}")), qPrintable(pageText));

        // The formula is sound: it waits for the verification, it is not
        // rejected.
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
    }

    // The first recognition of a text-less block (layout markup) writes the
    // text into box.text and leaves the block gray: no verdict has been made
    // yet (ADR 153).
    void firstRecognitionFillsBlockTextWithoutVerdict()
    {
        DetTokenChatServer server;
        server.layoutContent = QStringLiteral("<|box_start|>112 132 884 309<|box_end|><|ref_start|>text<|ref_end|>");
        server.transcriptionSequence.clear();
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("markup-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        // The markup pass alone: the block waits for its text-less recognition.
        m_controller->layoutScopePages();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->layoutBusy(), kImportTimeoutMs);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 1);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::TextRole).toString(), QString());

        m_controller->setSelectedBoxIndex(0);
        m_controller->recognizeSelectedBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 2, kImportTimeoutMs);

        // A block without text travels as the plain transcription prompt: no
        // recheck framing. chatRequests(0) is the page OCR call.
        QCOMPARE(server.chatRequests().size(), 2);
        QVERIFY(!server.chatRequests().last().contains("Previously recognized text:"));

        // The text lands in the block and the page text; the status stays gray.
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::TextRole).toString(), server.transcription);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedRole).toString(), QString());
        QCOMPARE(m_controller->selectedBlockText(), server.transcription);
        QCOMPARE(m_controller->resultText(), server.transcription);
        // The pipeline's text is not a manual edit: no «Изменено», nothing
        // for Revert to undo — on the page model's marker too.
        QVERIFY(!m_controller->currentPageEdited());
        auto *pages = qobject_cast<PageListModel *>(m_controller->pageModel());
        QVERIFY(pages);
        QVERIFY(!pages->data(pages->index(0, 0), PageListModel::EditedRole).toBool());

        // A manual edit after the landing rebases on the rebuilt text, so
        // Revert goes back to it — not to some earlier session's state.
        m_controller->setCurrentPageText(m_controller->resultText() + QStringLiteral("junk"));
        QVERIFY(m_controller->currentPageEdited());
        QVERIFY(pages->data(pages->index(0, 0), PageListModel::EditedRole).toBool());

        m_controller->revertCurrentPageEdits();
        QCOMPARE(m_controller->resultText(), server.transcription);
        QVERIFY(!m_controller->currentPageEdited());
        QVERIFY(!pages->data(pages->index(0, 0), PageListModel::EditedRole).toBool());

        // A block landing on the manually edited page (the check/fix queue
        // drains while the user is already typing on a fresh page) must not
        // wipe the user's text out from under the panel: that silent reset is
        // what made «Отменить изменения» a no-op on the first click. The
        // landing only moves the revert target to the block-derived text.
        m_controller->setCurrentPageText(m_controller->resultText() + QStringLiteral("junk2"));
        QVERIFY(m_controller->currentPageEdited());
        m_controller->recognizeSelectedBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(m_controller->resultText(), server.transcription + QStringLiteral("junk2"));
        QVERIFY(m_controller->currentPageEdited());

        m_controller->revertCurrentPageEdits();
        QCOMPARE(m_controller->resultText(), server.transcription);
        QVERIFY(!m_controller->currentPageEdited());
    }

    // The second run judges the corrected text and confirms it: the Ok verdict
    // lands on the correction layer, the transcription stays the block text,
    // nothing left to re-judge.
    void confirmedVerdictLandsOnTheCorrectionLayer()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // the first run rejects everything
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));
        m_settings->setFixModelName(QStringLiteral("fix-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("collapse-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;
        const int correctedStatusRole = BoxListModel::CorrectedCheckStatusRole;

        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 5, kImportTimeoutMs);  // 1 layout + 2 transcriptions + 2 decision
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));

        // The fix is a separate action (ADR 178): the «Fix» button corrects
        // the rejected blocks.
        m_controller->fixAllProblemBlocks();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 7, kImportTimeoutMs);  // +2 fix

        server.systemoneProbability = 0.97;  // the corrected text matches now
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 9, kImportTimeoutMs);
        QCOMPARE(server.systemoneRequests(), 4);
        // The judged candidate was the corrected text, not the OCR original.
        QVERIFY(server.systemoneBodies().at(2).contains("Fixed block text"));
        QVERIFY(!server.systemoneBodies().at(2).contains("1. Introduction"));

        // Both blocks are confirmed on the correction layer: green there, the
        // text layer keeps its first-run Mismatch, the transcription is
        // untouched.
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(0), correctedStatusRole).toInt(), int(BoxCheckStatus::Ok));
        QCOMPARE(boxes->data(boxes->index(1), correctedStatusRole).toInt(), int(BoxCheckStatus::Ok));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::ConfidenceRole).toDouble(), 0.1);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedConfidenceRole).toDouble(), 0.97);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::TextRole).toString(), QStringLiteral("1. Introduction"));
        QCOMPARE(boxes->data(boxes->index(1), BoxListModel::TextRole).toString(), QStringLiteral("Second block text"));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedRole).toString(), QStringLiteral("Fixed block text"));

        // Answered: a third run asks nothing.
        m_controller->checkEnabledBlocksOnPage();
        QVERIFY(!m_controller->checkRunning());
        QCOMPARE(server.requests(), 9);
    }

    // The block-recognition entry points: the selected block is re-run through
    // the transcription model whatever its status, and the problem-block
    // commands pick only the Mismatch/Review marks. A re-recognized block has
    // no verdict anymore — the next verification run re-judges its fresh text.
    void recognizeBlockCommandsReRunOnlyTheMarkedBlocks()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // the check run marks everything Mismatch
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));
        server.transcriptionSequence.clear();  // every block reads `transcription`

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("recognize-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;

        // The per-block button works on an unchecked block too.
        m_controller->setSelectedBoxIndex(0);
        m_controller->recognizeSelectedBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 4, kImportTimeoutMs);  // 1 layout + 3 transcriptions
        QCOMPARE(server.systemoneRequests(), 0);                            // straight to the transcription stage
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::TextRole).toString(), server.transcription);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedRole).toString(), QString());
        // The re-recognition is a from-scratch transcription (ADR 163): the
        // previous OCR text stays out of the request.
        QVERIFY(!server.chatRequests().last().contains("Previously recognized text:"));

        // The check run re-judges the re-recognized block by its fresh text
        // alongside the still-unchecked one; below the threshold both mismatch
        // and stay marked (no automatic re-recognition).
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 6);  // 3 transcriptions + 1 re-recognition + 2 decision
        QCOMPARE(server.systemoneRequests(), 2);
        QVERIFY(server.systemoneBodies().first().contains("Re-recognized block text"));
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Mismatch));

        // The problem-block command re-runs exactly the marked ones.
        QVERIFY(m_controller->pageProblemRecognitionSupported());
        m_controller->recognizeProblemBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 8);
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::TextRole).toString(), server.transcription);
        QCOMPARE(boxes->data(boxes->index(1), BoxListModel::TextRole).toString(), server.transcription);

        // Nothing is marked anymore: the command has nothing to do and asks
        // no server.
        QVERIFY(!m_controller->pageProblemRecognitionSupported());
        QVERIFY(!m_controller->allProblemRecognitionSupported());
        m_controller->recognizeAllProblemBlocks();
        QVERIFY(!m_controller->checkRunning());
        QCOMPARE(server.requests(), 8);
    }

    // The book-level «Исправить» runs every Mismatch/Review block through the
    // fix protocol, and each protocol answer lands in its own shape: FIX → the
    // correction text, OK → nothing stored (the recognition's verdict
    // survives, ADR 185), REVIEW → the damaged verdict without a correction.
    void fixEntriesCoverTheProblemBlocks()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // the check run marks everything Mismatch
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));
        m_settings->setFixModelName(QStringLiteral("fix-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("fix-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;
        const int correctedRole = BoxListModel::CorrectedRole;
        const int correctedStatusRole = BoxListModel::CorrectedCheckStatusRole;
        const int fixStatusRole = BoxListModel::FixStatusRole;

        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 5);  // 1 layout + 2 transcriptions + 2 decision, no auto-fix

        server.fixReply = QStringLiteral("OK");
        QVERIFY(m_controller->allProblemRecognitionSupported());
        m_controller->fixAllProblemBlocks();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 7);
        // OK confirms the candidate: nothing is stored, and the verdict the
        // recognition earned is dropped — the fixer is not a verifier
        // (ADR 240), so the block goes back to the gray «recognized» state.
        QCOMPARE(boxes->data(boxes->index(0), correctedRole).toString(), QString());
        QCOMPARE(boxes->data(boxes->index(1), correctedRole).toString(), QString());
        QCOMPARE(boxes->data(boxes->index(0), correctedStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(boxes->data(boxes->index(0), fixStatusRole).toInt(), int(BoxFixStatus::NotNeeded));

        server.fixReply = QStringLiteral("REVIEW");
        m_controller->setSelectedBoxIndex(0);
        m_controller->fixSelectedBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 8);
        // REVIEW marks the correction layer damaged: red, no correction text.
        QCOMPARE(boxes->data(boxes->index(0), correctedStatusRole).toInt(), int(BoxCheckStatus::Review));
        QCOMPARE(boxes->data(boxes->index(0), correctedRole).toString(), QString());
        QCOMPARE(boxes->data(boxes->index(0), fixStatusRole).toInt(), int(BoxFixStatus::Damaged));
        // The sibling keeps the text the recognition produced.
        QCOMPARE(boxes->data(boxes->index(1), correctedRole).toString(), QString());

        // The user's own verdict (ADR 185): the block in view is marked
        // correct even though the decision model rejected it.
        m_controller->setSelectedBoxIndex(1);
        m_controller->approveSelectedBlock();
        QCOMPARE(boxes->data(boxes->index(1), statusRole).toInt(), int(BoxCheckStatus::Approved));

        // The fixer has answered both blocks (ADR 188): the badge is quiet and
        // the book-level fix has nothing to do — a block the fixer answered
        // waits for the operator, not for another fix run.
        QCOMPARE(m_controller->scopedPendingFixCount(), 0);
        m_controller->fixAllProblemBlocks();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 8);

        // The re-recognition is a different remedy: the decision model still
        // rejects the blocks, so the OCR model may read them again.
        QVERIFY(m_controller->allProblemRecognitionSupported());
    }

    // Moving a block empties it (ADR 241): the text was recognized for the
    // region the block used to cover, so the block goes back to markup — no
    // text, no verdict — and the page text follows.
    void movingABlockDropsItsTextAndVerdict()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // the check run marks everything Mismatch
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("move-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);
        QVERIFY(m_controller->resultText().contains(QStringLiteral("1. Introduction")));

        // The decision model rejects the block: it is red before the move.
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::Mismatch));

        // The user moves it: the text belonged to the old region.
        m_controller->onBoxRectChanged(0, 0.5, 0.5, 0.2, 0.1);

        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::TextRole).toString(), QString());
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedRole).toString(), QString());
        // The page text followed the blocks.
        QVERIFY(!m_controller->resultText().contains(QStringLiteral("1. Introduction")));
        QVERIFY(m_controller->resultText().contains(QStringLiteral("Second block text")));
        // The sibling is untouched.
        QCOMPARE(boxes->data(boxes->index(1), BoxListModel::TextRole).toString(), QStringLiteral("Second block text"));
    }

    // The «Исправить (N)» badge counts the blocks that still wait for the fixer
    // (ADR 188): the decision model's rejections minus the ones the fixer has
    // already answered. A correction the decision model rejects goes back to
    // the fixer.
    void theFixBadgeCountsTheBlocksWaitingForTheFixer()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // the check run marks everything Mismatch
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));
        m_settings->setFixModelName(QStringLiteral("fix-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("fix-badge-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(m_controller->scopedPendingFixCount(), 2);

        // The user's confirmation takes one block out of the queue.
        m_controller->setSelectedBoxIndex(1);
        m_controller->approveSelectedBlock();
        QCOMPARE(m_controller->scopedPendingFixCount(), 1);

        // The fixer answers the other one with a correction: the number is
        // gone, the badge is quiet.
        server.fixReply = QStringLiteral("FIX\nRecovered block text");
        m_controller->fixAllProblemBlocks();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(m_controller->scopedPendingFixCount(), 0);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedRole).toString(), QStringLiteral("Recovered block text"));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedCheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        // The fresh correction is the text the user reads (ADR 185).
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::UseCorrectedRole).toBool(), true);

        // The decision model rejects the correction: the block is a new
        // candidate for the fixer, and the badge counts it again.
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(m_controller->scopedPendingFixCount(), 1);
    }

    // The panel's toggle (ADR 185): the user reads the corrected text or the
    // original one, and the page text follows the layer in view.
    void theLayerToggleMovesThePreviewText()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setFixModelName(QStringLiteral("fix-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("layer-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));

        m_controller->setSelectedBoxIndex(0);
        m_controller->fixSelectedBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);

        // The correction is in view: the preview follows it.
        QVERIFY(m_controller->selectedBlockUsesCorrected());
        QVERIFY(m_controller->resultText().contains(QStringLiteral("Fixed block text")));

        // The user compares with the original: the preview follows the text
        // layer.
        m_controller->setSelectedBlockUsesCorrected(false);
        QVERIFY(!m_controller->selectedBlockUsesCorrected());
        QVERIFY(m_controller->resultText().contains(QStringLiteral("1. Introduction")));
        QVERIFY(!m_controller->resultText().contains(QStringLiteral("Fixed block text")));

        m_controller->setSelectedBlockUsesCorrected(true);
        QVERIFY(m_controller->resultText().contains(QStringLiteral("Fixed block text")));
    }

    // The panel's diff view (ADR 186): the ranges point at the fragments the
    // correction changed.
    void theDiffRangesPointAtTheChangedFragments()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setFixModelName(QStringLiteral("fix-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("diff-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));

        // Without a correction there is nothing to compare: no range at all.
        m_controller->setSelectedBoxIndex(0);
        QVERIFY(m_controller->selectedBlockRemovedRanges().isEmpty());
        QVERIFY(m_controller->selectedBlockAddedRanges().isEmpty());

        m_controller->fixSelectedBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);

        const QVariantList removed = m_controller->selectedBlockRemovedRanges();
        const QVariantList added = m_controller->selectedBlockAddedRanges();
        QCOMPARE(removed.size(), 1);
        QCOMPARE(added.size(), 1);
        const QString original = m_controller->selectedBlockText();
        const QString corrected = m_controller->selectedBlockCorrected();
        const QVariantMap removedRange = removed.first().toMap();
        const QVariantMap addedRange = added.first().toMap();
        QCOMPARE(original.mid(removedRange.value(QStringLiteral("start")).toInt(), removedRange.value(QStringLiteral("length")).toInt()), original);
        QCOMPARE(corrected.mid(addedRange.value(QStringLiteral("start")).toInt(), addedRange.value(QStringLiteral("length")).toInt()), corrected);
        QVERIFY(!original.isEmpty());
        QVERIFY(!corrected.isEmpty());
    }

    // The hand-written text (ADR 187): it is stored in the version the user
    // wrote in, marked as the user's own text, and the page text follows it.
    void handWrittenTextIsMarkedAsEditedAndFollowsThePage()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("hand-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));

        m_controller->setSelectedBoxIndex(0);
        m_controller->setSelectedBlockText(QStringLiteral("Hand written text"), false);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        // The text is the user's own — not a confirmation of the model's verdict.
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::Edited));
        QCOMPARE(m_controller->selectedBlockText(), QStringLiteral("Hand written text"));
        QVERIFY(m_controller->resultText().contains(QStringLiteral("Hand written text")));
        // The version the user wrote in is the one the document carries.
        QVERIFY(!m_controller->selectedBlockUsesCorrected());
    }

    // «Отозвать утверждение» (ADR 187): the approval is undone and the
    // verdict the decision model gave stands again.
    void theApprovalCanBeRevoked()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // the check run marks everything Mismatch
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("revoke-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));

        m_controller->setSelectedBoxIndex(0);
        m_controller->approveSelectedBlock();
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Approved));
        // The confirmed block is not a problem anymore.
        QCOMPARE(m_controller->scopedPendingFixCount(), 1);

        m_controller->revertBlockApproval();
        QCOMPARE(boxes->data(boxes->index(0), statusRole).toInt(), int(BoxCheckStatus::Mismatch));
        // The model's verdict is back: the block is a problem again.
        QCOMPARE(m_controller->scopedPendingFixCount(), 2);
    }

    // The operator's rhythm (ADR 186): approving a block moves on to the next
    // problem block of the page.
    void approvingMovesToTheNextProblemBlock()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // the check run marks everything Mismatch
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("next-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(m_controller->scopedPendingFixCount(), 2);

        m_controller->setSelectedBoxIndex(0);
        m_controller->approveSelectedBlockAndSelectNext();

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::Approved));
        // The next block the decision model left open is in view.
        QCOMPARE(m_controller->selectedBoxIndex(), 1);

        // The last problem block: there is nothing behind it, so the view
        // stays where it is.
        m_controller->approveSelectedBlockAndSelectNext();
        QCOMPARE(m_controller->selectedBoxIndex(), 1);
    }

    // «Отменить исправление» must take the correction out of the preview too,
    // not only out of the block.
    void revertingTheCorrectionUpdatesThePreview()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setFixModelName(QStringLiteral("fix-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("revert-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusMessage()));

        m_controller->setSelectedBoxIndex(0);
        m_controller->fixSelectedBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QVERIFY(m_controller->resultText().contains(QStringLiteral("Fixed block text")));

        m_controller->revertBlockCorrection();
        QVERIFY2(!m_controller->resultText().contains(QStringLiteral("Fixed block text")), qPrintable(m_controller->resultText()));
        QVERIFY(m_controller->resultText().contains(QStringLiteral("1. Introduction")));
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
        m_settings->setModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("deverr-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        const int statusRole = BoxListModel::CheckStatusRole;

        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->checkRunning(), kImportTimeoutMs);
        QCOMPARE(server.requests(), 5);  // both blocks were asked
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

        m_controller->openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        QVERIFY(m_controller->hasResult());

        QVERIFY(m_controller->removeBlock(1));
        const QString afterRemoval = m_controller->resultText();
        QVERIFY(!afterRemoval.contains(QStringLiteral("Second block text")));
        // The removal is structural: the text still follows the blocks, so the
        // page is not a manual edit yet.
        QVERIFY(!m_controller->currentPageEdited());

        // Typing the very same text back is a no-op, not a revert.
        m_controller->setCurrentPageText(afterRemoval);
        QCOMPARE(m_controller->resultText(), afterRemoval);
        QVERIFY(!m_controller->currentPageEdited());
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

        m_controller->openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);
        recognizePage(*m_controller);
        QVERIFY(m_controller->hasResult());

        QVERIFY(m_controller->removeBlock(1));
        QCOMPARE(m_controller->resultText(), QStringLiteral("## 1. Introduction"));
        // A removal is a structural change: the page is not a manual edit, so
        // Revert has nothing to undo — and the removed block's text must not
        // come back.
        QVERIFY(!m_controller->currentPageEdited());

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

    // The «Изменено» label and the Revert button are separate QML bindings on
    // the edited flag; the flip must broadcast editStateChanged itself — the
    // panel's per-keystroke resultChanged echo used to be the only thing that
    // refreshed the button, and the label could stay on the pre-edit caption.
    void manualEditBroadcastsEditState()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        recognizePage(controller);
        QVERIFY(controller.hasResult());

        QSignalSpy editSpy(&controller, &AppController::editStateChanged);
        QVERIFY(editSpy.isValid());

        controller.setCurrentPageText(QStringLiteral("typed by hand"));
        QCOMPARE(controller.currentPageEdited(), true);
        QCOMPARE(editSpy.count(), 1);

        // Later keystrokes in the same edit session add no news.
        controller.setCurrentPageText(QStringLiteral("typed by hand again"));
        QCOMPARE(editSpy.count(), 1);

        controller.revertCurrentPageEdits();
        QCOMPARE(controller.currentPageEdited(), false);
        QCOMPARE(editSpy.count(), 2);
    }

    // The real WorkPanel.qml against the real controller: the user's repro is
    // typing into a mapped block's range right after recognition and clicking
    // «Отменить изменения» — the stored text must come back on the first click
    // (the QML-only replica of the panel logic could not reproduce a two-click
    // behavior, so the whole component runs here).
    void workPanelRevertUndoesBlockEditOnOneClick()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);
        recognizePage(controller);
        QVERIFY(controller.hasResult());
        const QString recognized = controller.resultText();

        qmlRegisterSingletonInstance("LLocr", 1, 0, "Controller", &controller);
        qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", m_settings.get());
        llocr::UiController uiController(*m_settings);
        qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", &uiController);
        // A disk-loaded WorkPanel.qml reaches the QML singletons through the
        // LLocr module; the engine here has no main.cpp registration cascade,
        // so the five module singletons are registered from their source files.
        const QString qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
        const auto registerSingleton = [&qmlRoot](const char *name) { qmlRegisterSingletonType(QUrl::fromLocalFile(qmlRoot + QStringLiteral("/%1.qml").arg(name)), "LLocr", 1, 0, name); };
        registerSingleton("BlockNames");
        registerSingleton("PageStatus");
        registerSingleton("RoleProfiles");
        registerSingleton("TaskStatus");
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        // Common/*.qml read Theme without importing LLocr (module-wide
        // visibility inside the app); a context property covers them when the
        // panel is loaded from disk.
        QQmlComponent themeComponent(&engine, QUrl::fromLocalFile(qmlRoot + QStringLiteral("/Theme.qml")));
        std::unique_ptr<QObject> theme{themeComponent.create()};
        QVERIFY2(theme, qPrintable(themeComponent.errorString()));
        engine.rootContext()->setContextProperty(QStringLiteral("Theme"), theme.get());
        const QString panelPath = QFINDTESTDATA(QStringLiteral("../src/qml/MainWindow/WorkPanel.qml"));
        QVERIFY2(QFile::exists(panelPath), qPrintable(panelPath));
        QQmlComponent component(&engine, QUrl::fromLocalFile(panelPath));
        std::unique_ptr<QQuickItem> panel{qobject_cast<QQuickItem *>(component.create())};
        QVERIFY2(panel, qPrintable(component.errorString()));
        panel->setWidth(760);
        panel->setHeight(600);

        QQuickWindow window;
        panel->setParentItem(window.contentItem());
        window.resize(780, 620);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QQuickItem *textArea = findFirstTextItem(panel.get());
        QVERIFY2(textArea, "the panel must contain its text field");
        QQuickItem *revertButton = findButtonWithText(panel.get(), QStringLiteral("Revert"));
        QVERIFY2(revertButton, "the panel must contain the Revert button");

        // The user reaches the caret by clicking into the text, which the
        // TapHandler turns into a block selection — the selected block and its
        // highlight are part of the repro.
        controller.setSelectedBoxIndex(0);
        textArea->forceActiveFocus();
        QVERIFY(textArea->hasActiveFocus());
        // The caret sits at position 0 — inside the first mapped block's range,
        // which is where the user's repro types.
        for (const int key : {int(Qt::Key_X), int(Qt::Key_Y), int(Qt::Key_Z)})
            QTest::keyClick(&window, Qt::Key(key));
        QCOMPARE(controller.currentPageEdited(), true);
        QCOMPARE(textArea->property("text").toString(), QStringLiteral("xyz") + recognized);
        QCOMPARE(revertButton->isVisible(), true);

        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, revertButton->mapToScene(QPointF(revertButton->width() / 2, revertButton->height() / 2)).toPoint());
        QCOMPARE(textArea->property("text").toString(), recognized);
        QCOMPARE(controller.currentPageEdited(), false);
        QCOMPARE(revertButton->isVisible(), false);
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

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster), QUrl::fromLocalFile(m_pdf)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 4, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);
        QVERIFY(controller.hasResult());
        QVERIFY(controller.removeBlock(1));
        // A removal is a structural change, not a manual edit.
        QVERIFY(!controller.currentPageEdited());
        controller.setCurrentPageText(controller.resultText() + QStringLiteral("\nhand-added line"));
        QVERIFY(controller.currentPageEdited());

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

    // The turn drops the page's recognition: the blocks were read from the
    // old turn and say nothing about the new one.
    void theTurnDropsTheRecognition()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);
        QVERIFY(controller.hasResult());
        auto *boxes = qobject_cast<BoxListModel *>(controller.boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);

        const QSize upright = controller.pageImage(0).size();
        controller.rotatePage(90);

        // The page is turned…
        QCOMPARE(controller.pageImage(0).size(), QSize(upright.height(), upright.width()));
        // …and the blocks it carried are gone: they were read from the old
        // turn, so they no longer sit where the text is.
        QCOMPARE(boxes->rowCount(), 0);
        QVERIFY(!controller.hasResult());
        QVERIFY(!controller.currentPageEditable());

        // A turn the other way is a turn like any other.
        controller.rotatePage(-90);
        QCOMPARE(controller.pageImage(0).size(), upright);
    }

    // The orientation check reads the picture alone: a page that lies on its
    // side is marked and left as it is, and no model is asked about it.
    void theOrientationCheckMarksASidewaysPageAndLeavesIt()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setOrientationCheckEnabled(true);
        m_settings->setDecisionModelName(QStringLiteral("decision-model"));

        const QImage source = rotateImage(syntheticTextPage(), 90);
        const QString path = m_dir.filePath(QStringLiteral("sideways.png"));
        QVERIFY(source.save(path, "PNG"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(path)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        // The status of every step: the check's own line is there.
        QStringList statuses;
        connect(&controller, &AppController::statusChanged, this, [&]() { statuses.append(controller.statusMessage()); });

        recognizePage(controller);

        // The page was not turned: the picture cannot tell which way it has to
        // be turned, so the user turns it by hand. The page is marked red.
        const QImage shown = controller.pageImage(0);
        QCOMPARE(shown.size(), source.size());
        QCOMPARE(controller.currentPageOrientationProblem(), int(PageOrientationProblem::Sideways));
        // No model was asked about the page.
        QCOMPARE(server.systemoneRequests(), 0);
        bool reported = false;
        for (const QString &status : statuses) {
            if (status.contains(QStringLiteral("side")))
                reported = true;
        }
        QVERIFY2(reported, qPrintable(statuses.join(QStringLiteral(" | "))));
    }

    // A page whose text runs across is left as it is and is not marked: the
    // picture read it and found nothing to report.
    void theOrientationCheckLeavesAnUprightPageUnmarked()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setOrientationCheckEnabled(true);
        m_settings->setDecisionModelName(QStringLiteral("decision-model"));

        const QImage source = syntheticTextPage();
        const QString path = m_dir.filePath(QStringLiteral("upright.png"));
        QVERIFY(source.save(path, "PNG"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(path)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);

        const QImage shown = controller.pageImage(0);
        QCOMPARE(shown.size(), source.size());
        QCOMPARE(qGray(shown.pixel(76, 40)), qGray(source.pixel(76, 40)));
        QCOMPARE(controller.currentPageOrientationProblem(), int(PageOrientationProblem::None));
        QCOMPARE(server.systemoneRequests(), 0);
    }

    // A skewed page is straightened before the text is read: the check
    // measures the skew, the renderer levels the lines, and the recognition
    // sees the straightened page. The second run finds nothing left to
    // straighten — the correction is stable.
    void theOrientationCheckStraightensASkewedPage()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setOrientationCheckEnabled(true);
        m_settings->setDecisionModelName(QStringLiteral("decision-model"));

        QTransform transform;
        transform.rotate(3.0);
        const QImage source = syntheticTextPage().transformed(transform, Qt::SmoothTransformation);
        const QString path = m_dir.filePath(QStringLiteral("skewed.png"));
        QVERIFY(source.save(path, "PNG"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(path)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);

        // The correction the check measured is on the page: the magnitude of
        // the skew the source was drawn with.
        QVERIFY2(std::abs(controller.currentPageSkewDegrees()) >= 1.5 && std::abs(controller.currentPageSkewDegrees()) <= 4.5,
                 qPrintable(QStringLiteral("the correction came out %1°").arg(controller.currentPageSkewDegrees())));
        // The straightened page grew: the bounding box of the turn.
        const QImage shown = controller.pageImage(0);
        QVERIFY(shown.width() > source.width() || shown.height() > source.height());
        QCOMPARE(controller.currentPageOrientationProblem(), int(PageOrientationProblem::None));

        // The straightened page reads as straight: the second check has
        // nothing to add.
        const double straightened = controller.currentPageSkewDegrees();
        recognizePage(controller);
        QCOMPARE(controller.currentPageSkewDegrees(), straightened);
    }

    // The markup pass carries the layout model's rotation tokens (ADR 245):
    // a sideways page whose blocks vote for a direction is turned by the
    // markup itself (ADR 246) and the markup runs again on the turned page —
    // the boxes the sideways layout read say nothing about the turned one.
    void theMarkupPassTurnsASidewaysPageAndRemarksIt()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setOrientationCheckEnabled(true);
        m_settings->setOrientationAutoTurnEnabled(true);
        m_settings->setDecisionModelName(QStringLiteral("decision-model"));
        m_settings->setLayoutRequestProfileId(QStringLiteral("mineru"));
        server.layoutContent = QStringLiteral("<|box_start|>113 132 885 310<|box_end|><|ref_start|>text<|ref_end|><|rotate_left|>\n\n"
                                              "<|box_start|>113 320 885 498<|box_end|><|ref_start|>text<|ref_end|><|rotate_left|>\n\n"
                                              "<|box_start|>113 508 885 686<|box_end|><|ref_start|>text<|ref_end|><|rotate_left|>");

        const QImage source = rotateImage(syntheticTextPage(), 90);
        const QString path = m_dir.filePath(QStringLiteral("sideways-autoturn.png"));
        QVERIFY(source.save(path, "PNG"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(path)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        // The markup pass runs on the sideways page — the pixel check reads the
        // axis first: the tokens vote left unanimously, the page turns to the
        // right — the mark goes with the turn — and the markup runs again on
        // the straightened page.
        controller.layoutCurrentPage();
        QTRY_VERIFY_WITH_TIMEOUT(controller.currentPageRotation() == 90, kImportTimeoutMs);
        QCOMPARE(controller.currentPageOrientationProblem(), int(PageOrientationProblem::None));
        QCOMPARE(controller.currentPageSuggestedTurn(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(controller.hasResult(), kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.layoutBusy(), kImportTimeoutMs);
        // The second pass read the boxes from the turned page: the page is
        // upright now and carries the markup.
        QCOMPARE(controller.pageImage(0).size(), source.size().transposed());

        // Marking up again changes nothing: the page is upright, the check
        // does not mark it, the vote has no sideways page to act on.
        controller.layoutCurrentPage();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.layoutBusy(), kImportTimeoutMs);
        QCOMPARE(controller.currentPageRotation(), 90);
        QCOMPARE(controller.currentPageOrientationProblem(), int(PageOrientationProblem::None));
    }

    // With the automatic turn off, the vote degrades to the one-click
    // suggestion: the mark and the advised direction stay, the page does not
    // move (ADR 242 — the user turns).
    void theMarkupPassSuggestsTheTurnWhenTheAutoTurnIsOff()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setOrientationCheckEnabled(true);
        m_settings->setOrientationAutoTurnEnabled(false);
        m_settings->setDecisionModelName(QStringLiteral("decision-model"));
        m_settings->setLayoutRequestProfileId(QStringLiteral("mineru"));
        server.layoutContent = QStringLiteral("<|box_start|>113 132 885 310<|box_end|><|ref_start|>text<|ref_end|><|rotate_left|>\n\n"
                                              "<|box_start|>113 320 885 498<|box_end|><|ref_start|>text<|ref_end|><|rotate_left|>\n\n"
                                              "<|box_start|>113 508 885 686<|box_end|><|ref_start|>text<|ref_end|><|rotate_left|>");

        const QImage source = rotateImage(syntheticTextPage(), 90);
        const QString path = m_dir.filePath(QStringLiteral("sideways-suggested.png"));
        QVERIFY(source.save(path, "PNG"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(path)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        // The markup pass runs on the sideways page — the pixel check reads the
        // axis first; with the automatic turn off the vote degrades to the
        // one-click suggestion: the mark and the advised direction stay.
        controller.layoutCurrentPage();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.layoutBusy(), kImportTimeoutMs);
        QCOMPARE(controller.currentPageSuggestedTurn(), 90);
        QCOMPARE(controller.currentPageOrientationProblem(), int(PageOrientationProblem::Sideways));

        // One click applies the suggestion: the page is turned, the mark and
        // the suggestion are gone, the markup was dropped with the old turn.
        controller.rotatePage(controller.currentPageSuggestedTurn());
        QCOMPARE(controller.currentPageOrientationProblem(), int(PageOrientationProblem::None));
        QCOMPARE(controller.currentPageSuggestedTurn(), 0);
        QCOMPARE(controller.pageImage(0).size(), source.size().transposed());
    }

    void theProjectKeepsTheTurnOfThePage()
    {
        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        controller.rotatePage(90);
        const QSize turned = controller.pageImage(0).size();
        QVERIFY(turned.width() != turned.height());

        const QString projectPath = m_dir.filePath(QStringLiteral("turned.llocr"));
        controller.saveProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.projectBusy(), kImportTimeoutMs);

        controller.openProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.projectBusy(), kImportTimeoutMs);
        QCOMPARE(controller.pageCount(), 1);
        QCOMPARE(controller.pageImage(0).size(), turned);
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

    // The auto pipeline (ADR 257): the run takes the document for itself and
    // its stages do their work. A stage whose list the queue refused would
    // look like a stage that did its job — the blocks would stay empty and
    // the run would report success — so the run may only start on a free
    // document, and its stages must find the queue free.
    void theAutoPipelineRunsTheStagesOverTheScope()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setLayoutModelName(QStringLiteral("det-token-test"));
        m_settings->setDecisionModelName(QStringLiteral("check-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("auto-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        // The button is live: the document is there and the models are picked.
        QVERIFY(m_controller->autoPipelineAvailable());
        QCOMPARE(m_controller->autoPipelineStageCount(), 5);

        m_controller->startAutoPipeline();
        QVERIFY(m_controller->autoPipelineRunning());
        // The run holds the document: the other actions are off.
        QVERIFY(!m_controller->actionsIdle());
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->autoPipelineRunning(), kImportTimeoutMs);

        // The markup and the recognition did their work: the blocks carry the
        // transcribed text, and the decision answered them. A refused list
        // would have left the blocks empty.
        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::TextRole).toString(), QStringLiteral("1. Introduction"));
        QCOMPARE(boxes->data(boxes->index(1), BoxListModel::TextRole).toString(), QStringLiteral("Second block text"));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::Ok));
        QCOMPARE(boxes->data(boxes->index(1), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::Ok));
        // 1 layout + 2 transcriptions + 2 decision requests.
        QCOMPARE(server.requests(), 5);
        QCOMPARE(server.systemoneRequests(), 2);
        // The markup stage did its job: the run does not name a failure.
        QVERIFY2(!m_controller->statusMessage().contains(QStringLiteral("The markup pass failed")), qPrintable(m_controller->statusMessage()));
        // The run's outcome reaches the user: the footer says the pipeline is
        // done, and the caveats of the run follow it.
        QVERIFY2(m_controller->statusMessage().contains(QStringLiteral("The automatic pipeline is done.")), qPrintable(m_controller->statusMessage()));
        // The footer reads the run's line from its own channel: the shared
        // status message belongs to every task, and the check that follows the
        // run would shadow the outcome.
        QVERIFY2(m_controller->autoPipelineMessage().contains(QStringLiteral("The automatic pipeline is done.")), qPrintable(m_controller->autoPipelineMessage()));
    }

    // A stopped run says so: the button reverts, but the footer must name
    // the outcome — a run that stops without a word reads as a run that
    // never happened.
    void theAutoPipelineSaysItWasStopped()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setLayoutModelName(QStringLiteral("det-token-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("auto-stopped-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        QVERIFY(m_controller->autoPipelineAvailable());
        m_controller->startAutoPipeline();
        // The markup pass is in flight: the stop lands on it.
        m_controller->stopAutoPipeline();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->autoPipelineRunning(), kImportTimeoutMs);

        QVERIFY2(m_controller->statusMessage().contains(QStringLiteral("The automatic pipeline was stopped.")), qPrintable(m_controller->statusMessage()));
    }

    // The run's line does not outlive the next task: a check that starts after
    // the run takes the footer line, so the manual flow's own line is not
    // shadowed by the outcome of the run.
    void theAutoPipelineLineGivesWayToTheNextCheck()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setLayoutModelName(QStringLiteral("det-token-test"));
        // No decision model: the verification stage is passed over and the
        // blocks keep no verdict, so the check below has work to do.

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("auto-line-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        QVERIFY(m_controller->autoPipelineAvailable());
        m_controller->startAutoPipeline();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->autoPipelineRunning(), kImportTimeoutMs);

        // The run has something to say: its outcome and the stage it passed
        // over.
        QVERIFY(!m_controller->autoPipelineMessage().isEmpty());

        // The manual check starts: the queue takes the footer line.
        m_controller->checkEnabledBlocksOnPage();
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->autoPipelineMessage().isEmpty(), kImportTimeoutMs);
    }

    // The stages are wired to each other: the fix's output feeds the recheck.
    // The fifth stage must judge the corrected text and not the OCR original —
    // the wiring is what makes the run a cycle and not five separate passes.
    void theAutoPipelineFeedsTheFixIntoTheRecheck()
    {
        DetTokenChatServer server;
        server.systemoneProbability = 0.1;  // the decision rejects every block
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setLayoutModelName(QStringLiteral("det-token-test"));
        m_settings->setDecisionModelName(QStringLiteral("check-test"));
        m_settings->setFixModelName(QStringLiteral("fix-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("auto-fix-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        QVERIFY(m_controller->autoPipelineAvailable());
        m_controller->startAutoPipeline();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->autoPipelineRunning(), kImportTimeoutMs);

        // 1 layout + 2 transcriptions + 2 decisions + 2 fixes + 2 decisions.
        QCOMPARE(server.requests(), 9);
        QCOMPARE(server.systemoneRequests(), 4);

        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);
        // The fix reached the block: the correction is there.
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedRole).toString(), QStringLiteral("Fixed block text"));
        // The recheck judged the correction: its verdict is on the correction
        // layer, while the text layer keeps the verdict of the first check.
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(boxes->data(boxes->index(0), BoxListModel::CorrectedCheckStatusRole).toInt(), int(BoxCheckStatus::Mismatch));
        // The judged candidate was the corrected text, not the OCR original.
        QVERIFY(server.systemoneBodies().at(2).contains("Fixed block text"));
        QVERIFY(!server.systemoneBodies().at(2).contains("1. Introduction"));
    }

    // A markup pass that fails must end the run: a stage that never reports
    // leaves the pipeline hanging, and nothing in the suite waits for it.
    void theAutoPipelineEndsWhenTheMarkupPassFails()
    {
        // No server: the markup request fails, the pages stay unmarked, and
        // the pass has nothing to show for itself.
        m_settings->setBaseUrl(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("no-server"))).toString());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setLayoutModelName(QStringLiteral("det-token-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("auto-failed-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        QVERIFY(m_controller->autoPipelineAvailable());
        m_controller->startAutoPipeline();
        QVERIFY(m_controller->autoPipelineRunning());

        // The run ends — it does not wait for a pass that never comes.
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->autoPipelineRunning(), kImportTimeoutMs);
        // The stage reported the failure instead of a success: the run says
        // which stage failed.
        QVERIFY2(m_controller->statusMessage().contains(QStringLiteral("The markup pass failed")), qPrintable(m_controller->statusMessage()));
        // The pages stayed unmarked: the pass did nothing.
        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 0);
    }

    // The gate follows the layout role of the request profile: the first
    // stage is passed over without it, so the button's state has to follow
    // the role's appearance and disappearance.
    void theAutoPipelineFollowsTheLayoutRole()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setLayoutModelName(QStringLiteral("det-token-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("auto-role-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        QVERIFY(m_controller->autoPipelineAvailable());

        // The layout model of the request profile loses its layout role: the
        // first stage would be passed over, so the button goes off — and the
        // pipeline's own signal says so, or the button keeps its old state.
        QSignalSpy spy(m_controller.get(), &AppController::autoStateChanged);
        m_layoutRequestProfiles->setModelProfiles({});
        m_settings->setLayoutRequestProfileId(QStringLiteral("without-a-layout-role"));
        QVERIFY(!m_controller->autoPipelineAvailable());
        QVERIFY(spy.count() > 0);

        // The role is back: the button is live again, and the signal says so.
        const int before = spy.count();
        m_layoutRequestProfiles->setModelProfiles(ModelProfiles::instance());
        m_settings->setLayoutRequestProfileId(QStringLiteral("mineru"));
        QVERIFY(m_controller->autoPipelineAvailable());
        QVERIFY(spy.count() > before);
    }

    // A markup reply without boxes leaves the page without blocks: the pass
    // marked nothing, so it failed — even though the reply was not empty.
    void theAutoPipelineEndsWhenTheMarkupPassMarksNoPage()
    {
        DetTokenChatServer server;
        server.layoutContent = QStringLiteral("Plain reply without layout tokens");
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setLayoutModelName(QStringLiteral("det-token-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("auto-boxless-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        m_controller->startAutoPipeline();
        QVERIFY(m_controller->autoPipelineRunning());

        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->autoPipelineRunning(), kImportTimeoutMs);
        QVERIFY2(m_controller->statusMessage().contains(QStringLiteral("The markup pass failed")), qPrintable(m_controller->statusMessage()));
        auto *boxes = qobject_cast<BoxListModel *>(m_controller->boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 0);
    }

    // The run must not start while another job holds the document: the queue
    // refuses the list of a stage whose work arrives while the layout runs,
    // and a refused list looks like a stage that did its job.
    void theAutoPipelineRefusesToStartWhileTheDocumentIsBusy()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        m_settings->setLayoutModelName(QStringLiteral("det-token-test"));

        QImage page(480, 360, QImage::Format_RGB32);
        page.fill(Qt::yellow);
        const QString raster = m_dir.filePath(QStringLiteral("auto-busy-raster.png"));
        QVERIFY(page.save(raster));

        m_controller->openFiles({QUrl::fromLocalFile(raster)});
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->importing(), kImportTimeoutMs);

        // The markup pass takes the layout.
        m_controller->layoutScopePages();
        QVERIFY(m_controller->layoutBusy());

        QVERIFY(!m_controller->autoPipelineAvailable());
        m_controller->startAutoPipeline();
        QVERIFY(!m_controller->autoPipelineRunning());
        QVERIFY2(m_controller->statusMessage().contains(QStringLiteral("Another job is running")), qPrintable(m_controller->statusMessage()));

        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->layoutBusy(), kImportTimeoutMs);
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
        m_controller = std::make_unique<AppController>(*m_settings, *m_runtime, *m_checkRequestProfiles, *m_layoutRequestProfiles, *m_fixRequestProfiles, *m_verification);
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
