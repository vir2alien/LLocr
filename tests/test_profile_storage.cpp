#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "config/ProfileStorage.h"
#include "config/ProfileStore.h"

using namespace llocr;

namespace {

// The smallest profile type ProfileStore's contract needs: an id key, JSON
// round-trip, equality and the merge hook (never invoked without built-ins).
struct FakeProfile {
    QString id;
    int value = 0;

    bool operator==(const FakeProfile &o) const { return id == o.id && value == o.value; }
    static FakeProfile merge(const FakeProfile &, const FakeProfile &user) { return user; }
    static QList<FakeProfile> profilesFromJson(const QJsonObject &root, QString &)
    {
        QList<FakeProfile> out;
        const QJsonArray arr = root.value(QStringLiteral("profiles")).toArray();
        for (const auto &v : arr) {
            const QJsonObject o = v.toObject();
            FakeProfile p;
            p.id = o.value(QStringLiteral("id")).toString();
            p.value = o.value(QStringLiteral("value")).toInt();
            out.append(p);
        }
        return out;
    }
    QJsonObject toJson() const { return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("value"), value}}; }
};

}  // namespace

class TestProfileStorage : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString path(const QString &name) const { return m_dir.filePath(name); }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void removeFileIfExistsHandlesMissingFile() { QVERIFY(ProfileStorage::removeFileIfExists(path(QStringLiteral("missing.json")))); }

    void removeFileIfExistsRemovesAndReportsFailures()
    {
        const QString p = path(QStringLiteral("doomed.json"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();

        QVERIFY(ProfileStorage::removeFileIfExists(p));
        QVERIFY(!QFile::exists(p));
    }

    void writeJsonAtomicCreatesAndOverwrites()
    {
        const QString p = path(QStringLiteral("nested/dir/catalog.json"));

        QJsonObject first;
        first.insert(QStringLiteral("schemaVersion"), 1);
        first.insert(QStringLiteral("value"), QStringLiteral("one"));
        QVERIFY(ProfileStorage::writeJsonAtomic(p, first));
        QVERIFY(QFile::exists(p));

        QJsonObject second;
        second.insert(QStringLiteral("schemaVersion"), 2);
        second.insert(QStringLiteral("items"), QJsonArray{QLatin1String("a")});
        QVERIFY(ProfileStorage::writeJsonAtomic(p, second));

        bool ok = false;
        QString error;
        const QJsonDocument doc = ProfileStorage::readJson(p, &ok, &error);
        QVERIFY(ok);
        QVERIFY(error.isEmpty());
        QCOMPARE(doc.object().value(QStringLiteral("schemaVersion")), 2);
        QCOMPARE(doc.object().value(QStringLiteral("items")).toArray().size(), 1);
    }

    void readJsonReportsMissingFile()
    {
        bool ok = true;
        QString error;
        const QJsonDocument doc = ProfileStorage::readJson(path(QStringLiteral("nope.json")), &ok, &error);
        QVERIFY(!ok);
        QVERIFY(doc.isNull());
        QVERIFY(error.isEmpty());  // missing file is reported via ok, not error
    }

    void readJsonReportsMalformedContent()
    {
        const QString p = path(QStringLiteral("broken.json"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ not json");
        f.close();

        bool ok = true;
        QString error;
        const QJsonDocument doc = ProfileStorage::readJson(p, &ok, &error);
        QVERIFY(!ok);
        QVERIFY(doc.isNull());
        QVERIFY(!error.isEmpty());
    }

    void readJsonParsesValidContent()
    {
        const QString p = path(QStringLiteral("valid.json"));
        QJsonObject root;
        root.insert(QStringLiteral("k"), QStringLiteral("v"));
        QVERIFY(ProfileStorage::writeJsonAtomic(p, root));

        bool ok = false;
        QString error;
        const QJsonDocument doc = ProfileStorage::readJson(p, &ok, &error);
        QVERIFY(ok);
        QVERIFY(error.isEmpty());
        QCOMPARE(doc.object().value(QStringLiteral("k")).toString(), QStringLiteral("v"));
    }

    void readEnvelopeRejectsMissingAndNewerVersions()
    {
        const QString noVersion = path(QStringLiteral("noversion.json"));
        QVERIFY(ProfileStorage::writeJsonAtomic(noVersion, QJsonObject{}));
        bool ok = true;
        bool tooNew = true;
        QString error;
        ProfileStorage::readEnvelope(noVersion, 2, QStringLiteral("Test"), &ok, &error, &tooNew);
        QVERIFY(!ok);
        QVERIFY(!tooNew);
        QCOMPARE(error, QStringLiteral("no schemaVersion"));

        const QString newer = path(QStringLiteral("newer.json"));
        QJsonObject newerBody;
        newerBody.insert(QStringLiteral("schemaVersion"), 3);
        QVERIFY(ProfileStorage::writeJsonAtomic(newer, newerBody));
        ok = true;
        tooNew = false;
        error.clear();
        ProfileStorage::readEnvelope(newer, 2, QStringLiteral("Test"), &ok, &error, &tooNew);
        QVERIFY(!ok);
        QVERIFY(tooNew);
        QVERIFY2(error.contains(QStringLiteral("newer")), qPrintable(error));

        ok = false;
        tooNew = true;
        error.clear();
        ProfileStorage::readEnvelope(newer, 3, QStringLiteral("Test"), &ok, &error, &tooNew);
        QVERIFY(ok);
        QVERIFY(!tooNew);
        QVERIFY(error.isEmpty());
    }

    // A user file written by a newer app version is neither loaded nor
    // overwritten: a save (or a reset) in the old format would silently
    // destroy whatever the newer version stored there.
    void storeRefusesToOverwriteNewerSchemaFile()
    {
        const QString userPath = path(QStringLiteral("profiles.json"));
        QJsonObject body;
        body.insert(QStringLiteral("schemaVersion"), 99);
        body.insert(QStringLiteral("profiles"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("p1")}, {QStringLiteral("value"), 7}}});
        QVERIFY(ProfileStorage::writeJsonAtomic(userPath, body));

        ProfileStore<FakeProfile> store(QString(), 2, QStringLiteral("TestStore"));
        store.setUserPath(userPath);
        store.reloadUserProfiles();
        QVERIFY(store.merged(QStringLiteral("p1")).id.isEmpty());

        FakeProfile p;
        p.id = QStringLiteral("p1");
        p.value = 8;
        QVERIFY(!store.putUserProfile(p));
        QVERIFY(!store.removeUserProfile(QStringLiteral("p1")));
        store.resetToBuiltIn();

        bool ok = false;
        QString error;
        ProfileStorage::readEnvelope(userPath, 99, QStringLiteral("Test"), &ok, &error);
        QVERIFY(ok);  // still version 99 — the store did not touch the file
    }

    void storePersistsAndReloadsUserProfiles()
    {
        const QString userPath = path(QStringLiteral("normal.json"));
        ProfileStore<FakeProfile> store(QString(), 2, QStringLiteral("TestStore"));
        store.setUserPath(userPath);

        FakeProfile p;
        p.id = QStringLiteral("p1");
        p.value = 5;
        QVERIFY(store.putUserProfile(p));
        QVERIFY(QFile::exists(userPath));

        ProfileStore<FakeProfile> again(QString(), 2, QStringLiteral("TestStore"));
        again.setUserPath(userPath);
        again.reloadUserProfiles();
        QCOMPARE(again.merged(QStringLiteral("p1")).value, 5);
        QVERIFY(again.removeUserProfile(QStringLiteral("p1")));
        QVERIFY(!QFile::exists(userPath));
    }

    // A write failure reaches the caller instead of a faked success: the
    // target here is a directory, so QSaveFile cannot commit.
    void putUserProfileReportsWriteFailure()
    {
        const QString userPath = path(QStringLiteral("blocker"));
        QVERIFY(QDir().mkpath(userPath));
        ProfileStore<FakeProfile> store(QString(), 2, QStringLiteral("TestStore"));
        store.setUserPath(userPath);

        FakeProfile p;
        p.id = QStringLiteral("p1");
        QVERIFY(!store.putUserProfile(p));
    }
};

QTEST_MAIN(TestProfileStorage)
#include "test_profile_storage.moc"
