// Test double for llama-server (§8). Deliberately tiny: it answers
// --version/--help, serves /health and /v1/models over HTTP, prints model-load
// looking lines, and can be driven into a crash or a delayed start. It never
// touches the real network — it binds to loopback only.
//
// Flags (all optional):
//   --port N            listen on N (default: 0 → ephemeral)
//   --version-out X     override --version stdout (for probe tests)
//   --never-healthy     /health always returns 503
//   --delay-start MS    sleep before binding the socket
//   --crash-on-health   exit(1) the first time /health is served
//   --crash-after MS    exit(1) after MS of uptime
//   --hello N           print N "llama_model_loader: loading" lines at startup

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <cstdlib>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    int port = 0;
    QString versionOut = QStringLiteral("build: 10594 (b10594)");
    bool neverHealthy = false;
    int delayStartMs = 0;
    bool crashOnHealth = false;
    qint64 crashAfterMs = 0;
    int helloLines = 0;

    QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args.at(i);
        const auto value = [&]() { return args.at(++i); };
        if (a == QStringLiteral("--port"))
            port = value().toInt();
        else if (a == QStringLiteral("--version-out"))
            versionOut = value();
        else if (a == QStringLiteral("--never-healthy"))
            neverHealthy = true;
        else if (a == QStringLiteral("--delay-start"))
            delayStartMs = value().toInt();
        else if (a == QStringLiteral("--crash-on-health"))
            crashOnHealth = true;
        else if (a == QStringLiteral("--crash-after"))
            crashAfterMs = value().toLongLong();
        else if (a == QStringLiteral("--hello"))
            helloLines = value().toInt();
    }

    // The probe runs `--version` / `--help`; keep them quick and truthful.
    bool ranVersionBreak = false;
    for (int i = 0; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QStringLiteral("--version")) {
            printf("%s\n", versionOut.toUtf8().constData());
            return 0;
        }
        if (a == QStringLiteral("--help")) {
            printf("usage: llama-server [options]\n"
                   "  --flash-attn [on|off|auto]\n"
                   "  --alias NAME\n"
                   "  --jinja\n"
                   "  -ctk TYPE\n"
                   "  -ctv TYPE\n");
            return 0;
        }
    }

    if (helloLines > 0) {
        for (int i = 0; i < helloLines; ++i)
            printf("llama_model_loader: loading model chunk %d\n", i);
        fflush(stdout);
    }
    if (delayStartMs > 0)
        QThread::msleep(static_cast<unsigned long>(delayStartMs));

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, port)) {
        fprintf(stderr, "mock: listen failed\n");
        return 2;
    }
    const int boundPort = server.serverPort();
    fprintf(stderr, "llama server listening on http://127.0.0.1:%d\n", boundPort);
    fflush(stderr);

    int healthHits = 0;

    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        while (QTcpSocket *s = server.nextPendingConnection()) {
            QObject::connect(s, &QTcpSocket::readyRead, [&, s]() {
                const QByteArray req = s->readAll();
                if (req.contains("GET /health")) {
                    healthHits++;
                    if (crashOnHealth && healthHits == 1) {
                        s->disconnectFromHost();
                        QTimer::singleShot(0, &app, []() { std::exit(1); });
                        return;
                    }
                    const QByteArray body = neverHealthy ? "unavailable" : "{\"status\":\"ok\"}";
                    const QByteArray status = neverHealthy ? "503 Service Unavailable" : "200 OK";
                    s->write("HTTP/1.1 " + status + "\r\n"
                             "Content-Type: application/json\r\n"
                             "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                             "Connection: close\r\n\r\n" + body);
                    s->flush();
                } else if (req.contains("GET /v1/models")) {
                    const QByteArray body =
                        QJsonDocument(QJsonObject{{"object", "list"},
                                                  {"data", QJsonArray{{QJsonObject{
                                                                           {"id", "llocr-local"},
                                                                           {"object", "model"}}}}}})
                            .toJson(QJsonDocument::Compact);
                    s->write("HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/json\r\n"
                             "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                             "Connection: close\r\n\r\n" + body);
                    s->flush();
                } else {
                    s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    s->flush();
                }
            });
            QObject::connect(s, &QTcpSocket::disconnected, s, &QTcpSocket::deleteLater);
        }
    });

    if (crashAfterMs > 0) {
        QTimer::singleShot(crashAfterMs, &app, []() { std::exit(1); });
    }
    // Keep stdout/stderr unbuffered so tests observe progress promptly.
    return app.exec();
}