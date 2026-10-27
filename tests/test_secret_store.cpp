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
    void aMissingKeyIsToldApartFromARefusal();
    void theFirstIdWithAValueWins();

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

// A key that is not there and a store that would not give the key are
// different answers, and the app has to tell them apart: the first one means
// "enter the key", the second one means "the item belongs to another build
// of the app" (ADR 233).
void TestSecretStore::aMissingKeyIsToldApartFromARefusal()
{
    QString value;
    QVERIFY(!SecretStore::read(m_id, &value));
    QCOMPARE(SecretStore::lastStatus(), SecretStore::Status::NotFound);
    QVERIFY(SecretStore::lastStatus() != SecretStore::Status::Refused);
}

// The rule of the request path: the task's own key, or the shared one. An id
// that is not there is skipped in silence — the task key is optional.
void TestSecretStore::theFirstIdWithAValueWins()
{
    const QString second = m_id + QStringLiteral("/second");
    QVERIFY(SecretStore::write(second, QStringLiteral("shared-value")));

    QString value;
    QString error;
    QVERIFY(SecretStore::readFirst({m_id, second}, &value, &error));
    QCOMPARE(value, QStringLiteral("shared-value"));
    QVERIFY(error.isEmpty());

    // The first id that holds a value wins.
    QVERIFY(SecretStore::write(m_id, QStringLiteral("own-value")));
    QVERIFY(SecretStore::readFirst({m_id, second}, &value, &error));
    QCOMPARE(value, QStringLiteral("own-value"));

    // Nothing there: no value, and the kind of the failure is "not found".
    const QString missing = m_id + QStringLiteral("/missing");
    QVERIFY(!SecretStore::readFirst({missing}, &value, &error));
    QCOMPARE(SecretStore::lastStatus(), SecretStore::Status::NotFound);

    SecretStore::remove(second);
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
