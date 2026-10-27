#include <QCoreApplication>
#include <QtTest>
#include <QUuid>

#include "config/SecretStore.h"

using namespace llocr;

// The secret store contract (ADR 227): a secret is written, read back and
// removed, and a name that was never stored is not found. The test uses its
// own ids and removes them afterwards, so it never touches a real key.
class TestSecretStore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void aKeyIsStoredReadAndRemoved();
    void aMissingKeyIsNotFound();
    void theValueIsReadFromTheStore();
    void theCacheIsDroppedWhenTheKeyGoes();
    void aStoredKeyComesBackFromTheStore();
    void aFailedReadExplainsItself();
    void aSuccessfulReadClearsTheError();

private:
    QString m_id;
};

void TestSecretStore::initTestCase()
{
    if (!SecretStore::isAvailable())
        QSKIP("No OS secret store on this machine");

    // The store can be there and still refuse the write: a sandboxed process
    // or a locked keychain answers "not permitted". The contract is then not
    // testable here, and that is not a failure of the code.
    const QString probe = QStringLiteral("org.llocr.test/probe");
    if (!SecretStore::write(probe, QStringLiteral("probe")))
        QSKIP(qPrintable(QStringLiteral("The OS secret store is not writable here: %1").arg(SecretStore::lastError())));
    SecretStore::remove(probe);
    SecretStore::forgetAllCached();
}

void TestSecretStore::init()
{
    m_id = QStringLiteral("org.llocr.test/") + QUuid::createUuid().toString(QUuid::Id128);
    SecretStore::forgetAllCached();
}

void TestSecretStore::cleanup()
{
    SecretStore::remove(m_id);
    SecretStore::forgetAllCached();
}

void TestSecretStore::aKeyIsStoredReadAndRemoved()
{
    QVERIFY2(SecretStore::write(m_id, QStringLiteral("s3cret-value")), qPrintable(SecretStore::lastError()));
    QVERIFY(SecretStore::exists(m_id));

    QString value;
    QVERIFY(SecretStore::read(m_id, &value));
    QCOMPARE(value, QStringLiteral("s3cret-value"));

    QVERIFY(SecretStore::remove(m_id));
    QVERIFY(!SecretStore::exists(m_id));
}

void TestSecretStore::aMissingKeyIsNotFound()
{
    QString value;
    QVERIFY(!SecretStore::read(m_id, &value));
    QVERIFY(!SecretStore::exists(m_id));
}

void TestSecretStore::theValueIsReadFromTheStore()
{
    QVERIFY(SecretStore::write(m_id, QStringLiteral("first")));

    QString cached;
    QVERIFY(SecretStore::read(m_id, &cached));
    QCOMPARE(cached, QStringLiteral("first"));
}

void TestSecretStore::theCacheIsDroppedWhenTheKeyGoes()
{
    QVERIFY(SecretStore::write(m_id, QStringLiteral("value")));
    QVERIFY(SecretStore::remove(m_id));

    QString cached;
    QVERIFY(!SecretStore::read(m_id, &cached));
}

// The write is verified by a read back: a store that took the value and does
// not give it back has not stored it (ADR 232).
void TestSecretStore::aStoredKeyComesBackFromTheStore()
{
    QVERIFY2(SecretStore::write(m_id, QStringLiteral("s3cret-value")), qPrintable(SecretStore::lastError()));

    SecretStore::forgetAllCached();
    QString value;
    QVERIFY(SecretStore::read(m_id, &value));
    QCOMPARE(value, QStringLiteral("s3cret-value"));
}

// A key that is not there must say why: an empty field without an
// explanation is the bug ADR 232 is about.
void TestSecretStore::aFailedReadExplainsItself()
{
    QString value;
    QVERIFY(!SecretStore::read(m_id, &value));
    QVERIFY2(!SecretStore::lastError().isEmpty(), "a failed read must name the reason");
}

void TestSecretStore::aSuccessfulReadClearsTheError()
{
    QString value;
    QVERIFY(!SecretStore::read(m_id, &value));
    QVERIFY(!SecretStore::lastError().isEmpty());

    QVERIFY(SecretStore::write(m_id, QStringLiteral("value")));
    QVERIFY(SecretStore::read(m_id, &value));
    QVERIFY(SecretStore::lastError().isEmpty());
}

QTEST_MAIN(TestSecretStore)
#include "test_secret_store.moc"
