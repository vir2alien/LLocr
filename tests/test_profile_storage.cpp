#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "app/ProfileStorage.h"

using namespace llocr;

class TestProfileStorage : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString path(const QString &name) const
    {
        return m_dir.filePath(name);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void removeFileIfExistsHandlesMissingFile()
    {
        QVERIFY(ProfileStorage::removeFileIfExists(path(QStringLiteral("missing.json"))));
    }

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
        const QJsonDocument doc =
            ProfileStorage::readJson(path(QStringLiteral("nope.json")), &ok, &error);
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
        QCOMPARE(doc.object().value(QStringLiteral("k")).toString(),
                 QStringLiteral("v"));
    }
};

QTEST_MAIN(TestProfileStorage)
#include "test_profile_storage.moc"
