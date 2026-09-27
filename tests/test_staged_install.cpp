#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "runtime/StagedInstall.h"

using namespace llocr;

// The publish-by-rename rule both install transactions rely on (ADR 112).
class TestStagedInstall : public QObject
{
    Q_OBJECT

private slots:
    void stagingIsCreated()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString staging = StagedInstall::stagingPathFor(dir.path(), QStringLiteral("model-x"));
        StagedInstall staged(staging, QDir(dir.path()).filePath(QStringLiteral("final")));
        QVERIFY(staged.isValid());
        QVERIFY(QFileInfo::exists(staging));
    }

    // The guarantee: an install that dies without committing leaves nothing.
    void uncommittedStagingIsRemovedOnDestruction()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString staging = StagedInstall::stagingPathFor(dir.path(), QStringLiteral("model-x"));
        {
            StagedInstall staged(staging, QDir(dir.path()).filePath(QStringLiteral("final")));
            QFile file(QDir(staging).filePath(QStringLiteral("model.gguf")));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("partial");
            file.close();
        }
        QVERIFY2(!QFileInfo::exists(staging), "a staging directory must not survive an uncommitted install");
    }

    void discardIsIdempotentAndExplicit()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString staging = StagedInstall::stagingPathFor(dir.path(), QStringLiteral("model-y"));
        StagedInstall staged(staging, QDir(dir.path()).filePath(QStringLiteral("final")));
        QVERIFY(QFileInfo::exists(staging));
        staged.discard();
        QVERIFY(!QFileInfo::exists(staging));
        staged.discard();  // no crash, still gone
        QVERIFY(!QFileInfo::exists(staging));
    }

    // commit() publishes atomically and replaces whatever was there.
    void commitPublishesAndReplacesThePreviousInstall()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString staging = StagedInstall::stagingPathFor(dir.path(), QStringLiteral("model-z"));
        const QString finalDir = QDir(dir.path()).filePath(QStringLiteral("build-1"));

        // A previous install is in place.
        QVERIFY(QDir().mkpath(finalDir));
        QFile old(QDir(finalDir).filePath(QStringLiteral("old.bin")));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("old");
        old.close();

        {
            StagedInstall staged(staging, finalDir);
            QFile fresh(QDir(staging).filePath(QStringLiteral("new.bin")));
            QVERIFY(fresh.open(QIODevice::WriteOnly));
            fresh.write("new");
            fresh.close();

            QString error;
            QVERIFY2(staged.commit(&error), qPrintable(error));
            // The staging directory is gone (it *is* the final directory now).
            QVERIFY(!QFileInfo::exists(staging));
            QVERIFY(QFileInfo::exists(finalDir));
            QVERIFY(QFileInfo::exists(QDir(finalDir).filePath(QStringLiteral("new.bin"))));
            QVERIFY2(!QFileInfo::exists(QDir(finalDir).filePath(QStringLiteral("old.bin"))), "the replaced install must be gone");
        }
        // No leftover .old-* backup directory.
        const QStringList leftovers = QDir(dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(leftovers, QStringList({QStringLiteral("build-1")}));
    }

    void committingTwiceIsRefused()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString staging = StagedInstall::stagingPathFor(dir.path(), QStringLiteral("model-w"));
        const QString finalDir = QDir(dir.path()).filePath(QStringLiteral("build-2"));
        StagedInstall staged(staging, finalDir);
        QString error;
        QVERIFY(staged.commit(&error));
        QVERIFY2(!staged.commit(&error), "a second commit must be refused");
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestStagedInstall)
#include "test_staged_install.moc"
