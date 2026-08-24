#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include "runtime/DownloadManager.h"
#include "runtime/DownloadTask.h"

using namespace llocr;

// ---------------------------------------------------------------------------
// Test double: a minimal HTTP/1.1 server on loopback that exercises the exact
// behaviours DownloadTask must handle (Range/If-Range, malformed Content-Range,
// validator changes, hold-open for cancellation). No real network is used.
// ---------------------------------------------------------------------------

namespace {

struct RecordedRequest {
    QString range;    // empty when no Range header
    QString ifRange;  // empty when no If-Range header
};

class TestServer : public QObject
{
public:
    QByteArray content;
    QString etag;
    bool honorRanges = false;
    bool corruptContentRange = false;
    bool streamPartialThenHold = false;

    QList<RecordedRequest> requests;

    bool start()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_buffer.insert(socket, QByteArray());
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
                    m_held.remove(socket);
                    m_buffer.remove(socket);
                    socket->deleteLater();
                });
            }
        });
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    int port() const { return m_server.serverPort(); }

    QString url(const QString &name) const
    {
        return QStringLiteral("http://127.0.0.1:%1/%2").arg(m_server.serverPort()).arg(name);
    }

private:
    // Declaration order matters for destruction: accepted sockets are children
    // of m_server, so these containers must be destroyed AFTER m_server (i.e.
    // declared before it). Reverse destruction order means m_server — and its
    // child sockets, whose `disconnected` signal re-enters the lambdas above —
    // is torn down first, while m_buffer/m_held are still valid.
    QHash<QTcpSocket *, QByteArray> m_buffer;
    QSet<QTcpSocket *> m_held;
    QTcpServer m_server;

    void onReadyRead(QTcpSocket *socket)
    {
        m_buffer[socket] += socket->readAll();
        const int end = m_buffer[socket].indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = m_buffer[socket].left(end);
        m_buffer[socket].remove(0, end + 4);
        handleRequest(socket, head);
    }

    static qint64 rangeStart(const QString &range)
    {
        if (!range.startsWith(QStringLiteral("bytes=")))
            return -1;
        const QString rest = range.mid(6);
        const int dash = rest.indexOf(QLatin1Char('-'));
        if (dash <= 0)
            return -1;
        return rest.left(dash).toLongLong();
    }

    void respond(QTcpSocket *socket, const QByteArray &statusLine, const QList<QByteArray> &headers,
                 const QByteArray &body, int declaredLength, bool keepOpen)
    {
        QByteArray raw;
        raw += statusLine + "\r\n";
        for (const QByteArray &header : headers)
            raw += header + "\r\n";
        raw += "Content-Length: " + QByteArray::number(declaredLength >= 0 ? declaredLength : body.size()) + "\r\n";
        raw += keepOpen ? QByteArray("Connection: keep-alive\r\n") : QByteArray("Connection: close\r\n");
        raw += "\r\n";
        raw += body;
        socket->write(raw);
        socket->flush();
        if (keepOpen)
            m_held.insert(socket);
        // Otherwise the client closes after reading the body (Connection: close).
    }

    QList<QByteArray> baseHeaders()
    {
        QList<QByteArray> headers;
        if (!etag.isEmpty())
            headers.append("ETag: " + etag.toUtf8());
        return headers;
    }

    void handleRequest(QTcpSocket *socket, const QByteArray &head)
    {
        const QList<QByteArray> lines = head.split('\n');
        QString range;
        QString ifRange;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            const QByteArray name = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (name == "range")
                range = QString::fromLatin1(value);
            else if (name == "if-range")
                ifRange = QString::fromLatin1(value);
        }
        requests.append({range, ifRange});

        if (!range.isEmpty() && honorRanges) {
            const bool validatorOk = ifRange.isEmpty() || ifRange == etag;
            if (!validatorOk) {
                // Validator changed → the server ignores Range and sends 200.
                respond(socket, "HTTP/1.1 200 OK", baseHeaders(), content, -1, false);
                return;
            }
            if (corruptContentRange) {
                // 206 with a Content-Range whose start mismatches the request.
                QList<QByteArray> headers = baseHeaders();
                headers.append("Content-Range: bytes 0-" + QByteArray::number(content.size() - 1)
                               + "/" + QByteArray::number(content.size()));
                respond(socket, "HTTP/1.1 206 Partial Content", headers, QByteArray(), 0, false);
                return;
            }
            const qint64 start = rangeStart(range);
            const QByteArray body = (start >= 0 && start < content.size()) ? content.mid(int(start)) : QByteArray();
            QList<QByteArray> headers = baseHeaders();
            headers.append("Content-Range: bytes " + QByteArray::number(start) + "-"
                           + QByteArray::number(start + body.size() - 1) + "/" + QByteArray::number(content.size()));
            respond(socket, "HTTP/1.1 206 Partial Content", headers, body, -1, false);
            return;
        }

        if (streamPartialThenHold) {
            const int half = content.size() / 2;
            respond(socket, "HTTP/1.1 200 OK", baseHeaders(), content.left(half), content.size(), true);
            return;
        }
        respond(socket, "HTTP/1.1 200 OK", baseHeaders(), content, -1, false);
    }
};

QByteArray pattern(int size)
{
    QByteArray data;
    data.reserve(size);
    for (int i = 0; i < size; ++i)
        data.append(char(i & 0xff));
    return data;
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

QString sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

DownloadTask::Request makeReq(const QString &url, const QString &targetDir, const QString &name,
                              const QString &sha = QString())
{
    DownloadTask::Request request;
    request.url = QUrl(url);
    request.targetDir = targetDir;
    request.fileName = name;
    request.sha256 = sha;
    return request;
}

bool seedPartial(const QString &dir, const QString &name, const QByteArray &data, int split, const QString &etag)
{
    const QString partPath = QDir(dir).filePath(name + QStringLiteral(".part"));
    const QString metaPath = partPath + QStringLiteral(".meta");
    QFile f(partPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data.left(split));
    f.close();

    QSaveFile sf(metaPath);
    if (!sf.open(QIODevice::WriteOnly))
        return false;
    QJsonObject o;
    o.insert(QStringLiteral("schemaVersion"), 1);
    o.insert(QStringLiteral("etag"), etag);
    o.insert(QStringLiteral("total"), double(data.size()));
    sf.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return sf.commit();
}

const QString kEtagV1 = QStringLiteral("\"v1\"");
const QString kEtagV2 = QStringLiteral("\"v2\"");

}  // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class TestDownloadManager : public QObject
{
    Q_OBJECT

private slots:
    void sanitizeFileNameBasics();
    void simpleDownload();
    void resumeFromSeededPartial();
    void changedValidatorForcesFullRedownload();
    void incorrectContentRangeRestartsFresh();
    void badSha256RemovesFile();
    void cancelKeepsPartial();
    void cancelDeletesPartial();
    void insufficientSpaceFails();
    void parallelLimitRespectsTwoSlots();
    void refusesInsecureUrlByDefault();
};

void TestDownloadManager::sanitizeFileNameBasics()
{
    QCOMPARE(sanitizeFileName(QStringLiteral("model.bin")), QStringLiteral("model.bin"));
    QCOMPARE(sanitizeFileName(QStringLiteral("CON")), QStringLiteral("_CON"));
    QCOMPARE(sanitizeFileName(QStringLiteral("nul.txt")), QStringLiteral("_nul.txt"));
    QCOMPARE(sanitizeFileName(QStringLiteral("../etc/passwd")), QStringLiteral("etcpasswd"));
    QCOMPARE(sanitizeFileName(QStringLiteral("a\\b")), QStringLiteral("ab"));
    QCOMPARE(sanitizeFileName(QStringLiteral("..")), QStringLiteral("download"));
    QCOMPARE(sanitizeFileName(QStringLiteral("   ")), QStringLiteral("download"));
    QCOMPARE(sanitizeFileName(QStringLiteral("name.")), QStringLiteral("name"));
}

void TestDownloadManager::simpleDownload()
{
    QTemporaryDir dir;
    TestServer server;
    const QByteArray data = pattern(4096);
    server.content = data;
    QVERIFY(server.start());

    DownloadManager mgr;
    mgr.setAllowLoopbackHttp(true);
    const int row = mgr.enqueue(makeReq(server.url("file.bin"), dir.path(), "file.bin", sha256Hex(data)));

    QCOMPARE(mgr.rowCount(), 1);
    QCOMPARE(mgr.roleNames().value(DownloadManager::NameRole), QByteArray("name"));
    QCOMPARE(mgr.data(mgr.index(0), DownloadManager::StateRole).toInt(), int(DownloadTask::State::Running));

    DownloadTask *task = mgr.taskAt(row);
    QTRY_COMPARE_WITH_TIMEOUT(int(task->state()), int(DownloadTask::State::Completed), 5000);

    QCOMPARE(mgr.data(mgr.index(0), DownloadManager::NameRole).toString(), QStringLiteral("file.bin"));
    QCOMPARE(task->receivedBytes(), qint64(data.size()));
    QCOMPARE(task->totalBytes(), qint64(data.size()));
    QCOMPARE(readFile(QDir(dir.path()).filePath("file.bin")), data);
    QVERIFY(!QFile::exists(QDir(dir.path()).filePath("file.bin.part")));
    QVERIFY(!QFile::exists(QDir(dir.path()).filePath("file.bin.part.meta")));
    QCOMPARE(server.requests.size(), 1);
    QVERIFY(server.requests.first().range.isEmpty());
}

void TestDownloadManager::resumeFromSeededPartial()
{
    QTemporaryDir dir;
    TestServer server;
    const QByteArray data = pattern(8192);
    server.content = data;
    server.etag = kEtagV1;
    server.honorRanges = true;
    QVERIFY(server.start());

    const int split = 3000;
    QVERIFY(seedPartial(dir.path(), "model.bin", data, split, kEtagV1));

    DownloadManager mgr;
    mgr.setAllowLoopbackHttp(true);
    const int row = mgr.enqueue(makeReq(server.url("model.bin"), dir.path(), "model.bin", sha256Hex(data)));
    DownloadTask *task = mgr.taskAt(row);
    QTRY_COMPARE_WITH_TIMEOUT(int(task->state()), int(DownloadTask::State::Completed), 5000);

    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.first().range, QStringLiteral("bytes=3000-"));
    QCOMPARE(server.requests.first().ifRange, kEtagV1);
    QCOMPARE(readFile(QDir(dir.path()).filePath("model.bin")), data);
}

void TestDownloadManager::changedValidatorForcesFullRedownload()
{
    QTemporaryDir dir;
    TestServer server;
    const QByteArray oldData(4000, 'A');
    const QByteArray newData(4000, 'B');
    server.content = newData;
    server.etag = kEtagV2;  // validator moved on since the partial was saved
    server.honorRanges = true;
    QVERIFY(server.start());

    QVERIFY(seedPartial(dir.path(), "model.bin", oldData, 2000, kEtagV1));

    DownloadManager mgr;
    mgr.setAllowLoopbackHttp(true);
    const int row = mgr.enqueue(makeReq(server.url("model.bin"), dir.path(), "model.bin", sha256Hex(newData)));
    DownloadTask *task = mgr.taskAt(row);
    QTRY_COMPARE_WITH_TIMEOUT(int(task->state()), int(DownloadTask::State::Completed), 5000);

    // The resume attempt carried the stale If-Range; the server answered 200 and
    // the downloader restarted from scratch (no splice of old + new bytes).
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.first().ifRange, kEtagV1);
    QCOMPARE(readFile(QDir(dir.path()).filePath("model.bin")), newData);
}

void TestDownloadManager::incorrectContentRangeRestartsFresh()
{
    QTemporaryDir dir;
    TestServer server;
    const QByteArray data = pattern(6000);
    server.content = data;
    server.etag = kEtagV1;
    server.honorRanges = true;
    server.corruptContentRange = true;
    QVERIFY(server.start());

    QVERIFY(seedPartial(dir.path(), "model.bin", data, 2000, kEtagV1));

    DownloadManager mgr;
    mgr.setAllowLoopbackHttp(true);
    const int row = mgr.enqueue(makeReq(server.url("model.bin"), dir.path(), "model.bin", sha256Hex(data)));
    DownloadTask *task = mgr.taskAt(row);
    QTRY_COMPARE_WITH_TIMEOUT(int(task->state()), int(DownloadTask::State::Completed), 5000);

    // First attempt resumed (206 with a bad Content-Range), the downloader
    // rejected it and re-requested the whole object without a Range header.
    QCOMPARE(server.requests.size(), 2);
    QVERIFY(!server.requests.at(0).range.isEmpty());
    QVERIFY(server.requests.at(1).range.isEmpty());
    QCOMPARE(readFile(QDir(dir.path()).filePath("model.bin")), data);
}

void TestDownloadManager::badSha256RemovesFile()
{
    QTemporaryDir dir;
    TestServer server;
    const QByteArray data = pattern(3000);
    server.content = data;
    QVERIFY(server.start());

    DownloadManager mgr;
    mgr.setAllowLoopbackHttp(true);
    const QString badSha(64, QLatin1Char('0'));
    const int row = mgr.enqueue(makeReq(server.url("f.bin"), dir.path(), "f.bin", badSha));
    DownloadTask *task = mgr.taskAt(row);
    QTRY_COMPARE_WITH_TIMEOUT(int(task->state()), int(DownloadTask::State::Failed), 5000);

    QVERIFY(task->error().contains(QStringLiteral("Checksum"), Qt::CaseInsensitive));
    const QString base = QDir(dir.path()).filePath("f.bin");
    QVERIFY(!QFile::exists(base));
    QVERIFY(!QFile::exists(base + ".part"));
    QVERIFY(!QFile::exists(base + ".part.meta"));
}

void TestDownloadManager::cancelKeepsPartial()
{
    QTemporaryDir dir;
    TestServer server;
    server.content = pattern(100000);
    server.streamPartialThenHold = true;
    QVERIFY(server.start());

    DownloadManager mgr;
    mgr.setAllowLoopbackHttp(true);
    const int row = mgr.enqueue(makeReq(server.url("big.bin"), dir.path(), "big.bin"));
    DownloadTask *task = mgr.taskAt(row);
    QTRY_VERIFY_WITH_TIMEOUT(task->receivedBytes() > 0, 5000);

    mgr.cancel(row, false);
    QTRY_COMPARE_WITH_TIMEOUT(int(task->state()), int(DownloadTask::State::Canceled), 5000);
    QVERIFY(QFile::exists(QDir(dir.path()).filePath("big.bin.part")));
}

void TestDownloadManager::cancelDeletesPartial()
{
    QTemporaryDir dir;
    TestServer server;
    server.content = pattern(100000);
    server.streamPartialThenHold = true;
    QVERIFY(server.start());

    DownloadManager mgr;
    mgr.setAllowLoopbackHttp(true);
    const int row = mgr.enqueue(makeReq(server.url("big.bin"), dir.path(), "big.bin"));
    DownloadTask *task = mgr.taskAt(row);
    QTRY_VERIFY_WITH_TIMEOUT(task->receivedBytes() > 0, 5000);

    mgr.cancel(row, true);
    QTRY_COMPARE_WITH_TIMEOUT(int(task->state()), int(DownloadTask::State::Canceled), 5000);
    QVERIFY(!QFile::exists(QDir(dir.path()).filePath("big.bin.part")));
    QVERIFY(!QFile::exists(QDir(dir.path()).filePath("big.bin.part.meta")));
}

void TestDownloadManager::insufficientSpaceFails()
{
    QTemporaryDir dir;
    TestServer server;
    server.content = pattern(5000);
    QVERIFY(server.start());

    DownloadManager mgr;
    mgr.setAllowLoopbackHttp(true);
    mgr.setFreeBytesQuery([](const QString &) { return qint64(0); });
    const int row = mgr.enqueue(makeReq(server.url("m.bin"), dir.path(), "m.bin"));
    DownloadTask *task = mgr.taskAt(row);
    QTRY_COMPARE_WITH_TIMEOUT(int(task->state()), int(DownloadTask::State::Failed), 5000);
    QVERIFY(task->error().contains(QStringLiteral("space"), Qt::CaseInsensitive));
}

void TestDownloadManager::parallelLimitRespectsTwoSlots()
{
    QTemporaryDir dir;
    TestServer server;
    server.content = pattern(2000);
    server.streamPartialThenHold = true;
    QVERIFY(server.start());

    DownloadManager mgr;
    mgr.setAllowLoopbackHttp(true);
    for (int i = 0; i < 4; ++i) {
        const QString name = QStringLiteral("f%1.bin").arg(i);
        mgr.enqueue(makeReq(server.url(name), dir.path(), name));
    }

    auto count = [&](DownloadTask::State state) {
        int n = 0;
        for (int i = 0; i < mgr.rowCount(); ++i)
            if (mgr.taskAt(i)->state() == state)
                ++n;
        return n;
    };

    QCOMPARE(count(DownloadTask::State::Running), 2);
    QCOMPARE(count(DownloadTask::State::Queued), 2);
    mgr.cancelAll(true);
}

void TestDownloadManager::refusesInsecureUrlByDefault()
{
    QTemporaryDir dir;
    DownloadManager mgr;  // allowLoopbackHttp stays false
    const int row = mgr.enqueue(makeReq(QStringLiteral("http://127.0.0.1:1/x"), dir.path(), "x"));
    DownloadTask *task = mgr.taskAt(row);
    QTRY_COMPARE_WITH_TIMEOUT(int(task->state()), int(DownloadTask::State::Failed), 3000);
    QVERIFY(task->error().contains(QStringLiteral("https"), Qt::CaseInsensitive));
}

QTEST_MAIN(TestDownloadManager)
#include "test_download_manager.moc"