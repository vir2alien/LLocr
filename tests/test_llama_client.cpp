#include <QtTest>

#include "core/LlamaClient.h"

using namespace llocr;

namespace {
// An address from the TEST-NET/unused range: the TCP connect phase cannot
// complete quickly — it either hangs until the connect timeout or fails with
// unreachable. Both outcomes resolve the future, which is what these tests
// assert; the reason is irrelevant.
constexpr const char *kBlackHoleHost = "10.255.255.1";
constexpr quint16 kBlackHolePort = 81;
}  // namespace

class TestLlamaClient : public QObject
{
    Q_OBJECT

private slots:
    // C++-I4: a second postJson used to overwrite the connect-phase members,
    // so the first future never finished (and its watcher leaked). Both
    // overlapping requests must reach a result.
    void overlappingConnectsBothFinish()
    {
        LlamaClient client;
        const QUrl url(QStringLiteral("http://%1:%2/v1/chat/completions").arg(kBlackHoleHost).arg(kBlackHolePort));
        QFuture<HttpResponse> first = client.postJson(url, "{}", {}, 4000, 4000);
        QFuture<HttpResponse> second = client.postJson(url, "{}", {}, 4000, 4000);

        QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(second.isFinished(), 15000);
        QVERIFY(!first.result().success);
        QVERIFY(!second.result().success);
        QVERIFY(!first.result().error.isEmpty());
        QVERIFY(!second.result().error.isEmpty());
    }

    void abortFinishesConnectPhase()
    {
        LlamaClient client;
        const QUrl url(QStringLiteral("http://%1:%2/v1/chat/completions").arg(kBlackHoleHost).arg(kBlackHolePort));
        QFuture<HttpResponse> future = client.postJson(url, "{}", {}, 8000, 8000);
        QVERIFY(!future.isFinished());

        client.abort();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 8000);
        QVERIFY(!future.result().success);
        QCOMPARE(future.result().error, QStringLiteral("Operation canceled"));
    }
};

QTEST_MAIN(TestLlamaClient)
#include "test_llama_client.moc"
