// The check of the shared server: the runtime settings ask an
// OpenAI-compatible server whether it answers, and the probe is what says so
// (the check and the status it shows).
//
// The probe talks to the mock llama-server: the loopback stand-in answers
// /v1/models, can insist on a bearer token, and can be pointed at a port
// nobody listens on.

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "config/SecretStore.h"
#include "config/SettingsStore.h"
#include "runtime/ConnectionProbe.h"
#include "testsettings.h"

using namespace llocr;

#ifndef LLOCR_MOCK_SERVER
#define LLOCR_MOCK_SERVER "mock_llama_server"
#endif

class TestConnectionProbe : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void theServerAnswers();
    void theModelListIsCounted();
    void theRightKeyIsAccepted();
    void theWrongKeyIsNamed();
    void aDeadServerIsReported();
    void anEmptyAddressIsReported();

private:
    // Starts the mock server and returns the port it bound to.
    int startServer(const QStringList &extraArgs);
    void stopServer();

    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<ConnectionProbe> m_probe;
    QProcess m_server;
};

void TestConnectionProbe::initTestCase()
{
    m_settings = std::make_unique<SettingsStore>();
    m_probe = std::make_unique<ConnectionProbe>(*m_settings);
}

void TestConnectionProbe::cleanupTestCase()
{
    stopServer();
}

int TestConnectionProbe::startServer(const QStringList &extraArgs)
{
    m_server.setProcessChannelMode(QProcess::MergedChannels);
    QStringList args = {QStringLiteral("--port"), QStringLiteral("0")};
    args.append(extraArgs);
    m_server.start(QLatin1String(LLOCR_MOCK_SERVER), args);
    if (!m_server.waitForStarted(10000))
        return 0;

    // The mock prints the port it bound to.
    int port = 0;
    QString output;
    const QRegularExpression portPattern(QStringLiteral("127\\.0\\.0\\.1:(\\d+)"));
    for (int waited = 0; waited < 100 && port == 0; ++waited) {
        // The process reads its pipe in the event loop, so the wait has to
        // pump it: a blocking sleep would leave the line in the pipe.
        m_server.waitForReadyRead(100);
        output += QString::fromUtf8(m_server.readAllStandardOutput());
        const QRegularExpressionMatch match = portPattern.match(output);
        if (match.hasMatch())
            port = match.captured(1).toInt();
    }
    return port;
}

void TestConnectionProbe::stopServer()
{
    if (m_server.state() != QProcess::NotRunning) {
        m_server.kill();
        m_server.waitForFinished(5000);
    }
}

void TestConnectionProbe::theServerAnswers()
{
    const int port = startServer({});
    QVERIFY(port > 0);

    m_probe->probe(QStringLiteral("http://127.0.0.1:%1").arg(port), QStringLiteral("test-key"));
    QTRY_VERIFY(!m_probe->running());

    QVERIFY(m_probe->ok());
    QCOMPARE(m_probe->httpStatus(), 200);
    QVERIFY(m_probe->latencyMs() >= 0);
    QVERIFY(m_probe->errorText().isEmpty());
    QVERIFY(!m_probe->running());

    stopServer();
}

void TestConnectionProbe::theModelListIsCounted()
{
    const int port = startServer({});
    QVERIFY(port > 0);

    m_probe->probe(QStringLiteral("http://127.0.0.1:%1").arg(port), QString());
    QTRY_VERIFY(!m_probe->running());

    QVERIFY(m_probe->ok());
    // The mock advertises one model.
    QCOMPARE(m_probe->modelCount(), 1);

    stopServer();
}

void TestConnectionProbe::theRightKeyIsAccepted()
{
    const int port = startServer({QStringLiteral("--api-key"), QStringLiteral("secret")});
    QVERIFY(port > 0);

    m_probe->probe(QStringLiteral("http://127.0.0.1:%1").arg(port), QStringLiteral("secret"));
    QTRY_VERIFY(!m_probe->running());

    QVERIFY(m_probe->ok());
    QCOMPARE(m_probe->httpStatus(), 200);

    stopServer();
}

// The server says the key is wrong: the answer must name that, not a bare
// "no answer".
void TestConnectionProbe::theWrongKeyIsNamed()
{
    const int port = startServer({QStringLiteral("--api-key"), QStringLiteral("secret")});
    QVERIFY(port > 0);

    m_probe->probe(QStringLiteral("http://127.0.0.1:%1").arg(port), QStringLiteral("wrong"));
    QTRY_VERIFY(!m_probe->running());

    QVERIFY(!m_probe->ok());
    QCOMPARE(m_probe->httpStatus(), 401);
    QVERIFY(m_probe->errorText().contains(QStringLiteral("key")));

    stopServer();
}

void TestConnectionProbe::aDeadServerIsReported()
{
    // A port nobody listens on: the loopback answer comes right away.
    m_probe->probe(QStringLiteral("http://127.0.0.1:9"), QString());
    QTRY_VERIFY(!m_probe->running());

    QVERIFY(!m_probe->ok());
    QVERIFY(!m_probe->errorText().isEmpty());
}

void TestConnectionProbe::anEmptyAddressIsReported()
{
    m_probe->probe(QString(), QString());
    QTRY_VERIFY(!m_probe->running());

    QVERIFY(!m_probe->ok());
    QVERIFY(!m_probe->errorText().isEmpty());
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_connection_probe"));
    TestConnectionProbe test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_connection_probe.moc"

