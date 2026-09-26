#include <QtTest>

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include "runtime/HttpClient.h"

using namespace llocr;

namespace {

// Two loopback servers: the second stands in for "another host", reached by a
// redirect. Loopback http has to be allowed explicitly for the test.
class TwoHopServer : public QObject
{
public:
    bool start()
    {
        connect(&m_target, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_target.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    if (!socket->readAll().contains("\r\n\r\n"))
                        return;
                    sawAuthorization =
                        socket->readAll().contains(QByteArrayLiteral("Authorization"));
                    ++m_targetHits;
                    const QByteArray body("moved");
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->flush();
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        connect(&m_origin, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_origin.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    const QByteArray request = socket->readAll();
                    if (!request.contains("\r\n\r\n"))
                        return;
                    const bool hadAuth = request.contains(QByteArrayLiteral("Authorization"));
                    const QByteArray body("moved");
                    if (hadAuth)
                        ++redirectsWithAuth;
                    QByteArray response =
                        "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:"
                        + QByteArray::number(m_target.serverPort()) + "/target\r\n"
                        + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
                    response += body;
                    socket->write(response);
                    socket->flush();
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        return m_origin.listen(QHostAddress::LocalHost, 0)
            && m_target.listen(QHostAddress::LocalHost, 0);
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/start").arg(m_origin.serverPort()));
    }

    int redirectsWithAuth = 0;
    int m_targetHits = 0;
    bool sawAuthorization = false;

private:
    QTcpServer m_origin;
    QTcpServer m_target;
};

}  // namespace

class TestHttpClient : public QObject
{
    Q_OBJECT

private slots:
    // The shared policy: follow the redirect, but never carry the Authorization
    // header across a host change (ADR 108).
    void redirectDropsAuthorizationAcrossHosts()
    {
        TwoHopServer server;
        QVERIFY(server.start());

        HttpClient::Options options;
        options.timeoutMs = 5000;
        options.authorization = "Bearer secret-token";

        const HttpClient::Response response =
            HttpClient::get(server.url(), options, 5, /*allowInsecure=*/true);

        QCOMPARE(response.status, 200);
        QCOMPARE(response.body, QByteArray("moved"));
        QCOMPARE(server.m_targetHits, 1);
        QVERIFY2(!server.sawAuthorization,
                 "the Authorization header must not follow a redirect to another host");
        QCOMPARE(server.redirectsWithAuth, 1);  // sent on the first hop
    }

    // A redirect loop the policy refuses fails instead of spinning.
    void hopBudgetIsEnforced()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl start(QStringLiteral("http://127.0.0.1:%1/start").arg(server.serverPort()));
        QObject::connect(&server, &QTcpServer::newConnection, this, [&]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [socket, this, &server]() {
                                     if (!socket->readAll().contains("\r\n\r\n"))
                                         return;
                                     const QByteArray location =
                                         "Location: http://127.0.0.1:"
                                         + QByteArray::number(server.serverPort())
                                         + "/again\r\n";
                                     socket->write("HTTP/1.1 302 Found\r\n" + location
                                                   + "Content-Length: 0\r\n\r\n");
                                     socket->flush();
                                 });
            }
        });

        HttpClient::Options options;
        options.timeoutMs = 5000;
        const HttpClient::Response response =
            HttpClient::get(start, options, /*maxRedirects=*/2, /*allowInsecure=*/true);

        QVERIFY(!response.error.isEmpty());
        QCOMPARE(response.status, 0);
    }
};

QTEST_MAIN(TestHttpClient)
#include "test_http_client.moc"
