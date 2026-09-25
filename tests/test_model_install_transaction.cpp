#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "app/LaunchProfileStore.h"
#include "app/SettingsStore.h"
#include "runtime/ModelCatalog.h"
#include "runtime/ModelInstallTransaction.h"
#include "runtime/ModelPreset.h"
#include "testsettings.h"

using namespace llocr;

namespace {

HfFile file(const QString &path, qint64 size, bool isDir = false)
{
    HfFile f;
    f.path = path;
    f.name = ModelCatalog::leafName(path);
    f.size = size;
    f.isDir = isDir;
    f.type = isDir ? QStringLiteral("directory") : QStringLiteral("file");
    return f;
}

}  // namespace

// Pure logic extracted from ModelInstaller: repo directory naming, model/mmproj
// selection from a HF tree, and the idle-state safety of cancel/shutdown.
class TestModelInstallTransaction : public QObject {
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_model_install_transaction"));
    }

    void repoDirNameSanitizes()
    {
        QCOMPARE(ModelInstallTransaction::repoDirName(QStringLiteral("org/repo")),
                 QStringLiteral("org__repo"));
        // Hostile characters and backslashes are replaced (per segment; the
        // joiner between segments is a double underscore).
        QCOMPARE(ModelInstallTransaction::repoDirName(QStringLiteral("o*g/r:e\\p")),
                 QStringLiteral("o_g__r_e_p"));
        // Dot segments are dropped.
        QCOMPARE(ModelInstallTransaction::repoDirName(QStringLiteral("../x/../repo")),
                 QStringLiteral("x__repo"));
        // Nothing usable falls back to a fixed name.
        QCOMPARE(ModelInstallTransaction::repoDirName(QStringLiteral("../..")),
                 QStringLiteral("model"));
        QCOMPARE(ModelInstallTransaction::repoDirName(QStringLiteral("  ")),
                 QStringLiteral("model"));
    }

    void selectPicksLargestSingle()
    {
        const QList<HfFile> tree = {
            file(QStringLiteral("model/small-Q4_K_M.gguf"), 10),
            file(QStringLiteral("model/big-Q8_0.gguf"), 100),
            file(QStringLiteral("README.md"), 1),
        };
        QStringList models;
        QString mmproj;
        ModelInstallTransaction::selectModelFiles(tree, QString(), QString(),
                                                  &models, mmproj);
        QCOMPARE(models.size(), 1);
        QCOMPARE(models.first(), QStringLiteral("model/big-Q8_0.gguf"));
        QVERIFY(mmproj.isEmpty());
    }

    void selectPicksPreferredSingle()
    {
        const QList<HfFile> tree = {
            file(QStringLiteral("a/big-Q8_0.gguf"), 100),
            file(QStringLiteral("b/small-Q4_K_M.gguf"), 10),
        };
        QStringList models;
        QString mmproj;
        ModelInstallTransaction::selectModelFiles(
            tree, QStringLiteral("small-Q4_K_M.gguf"), QString(), &models, mmproj);
        QCOMPARE(models.size(), 1);
        QCOMPARE(models.first(), QStringLiteral("b/small-Q4_K_M.gguf"));
    }

    void selectPicksLargestMultiPartGroup()
    {
        const QList<HfFile> tree = {
            file(QStringLiteral("m/model-00001-of-00002.gguf"), 50),
            file(QStringLiteral("m/model-00002-of-00002.gguf"), 60),
            file(QStringLiteral("m/single-Q4_K_M.gguf"), 80),
        };
        QStringList models;
        QString mmproj;
        ModelInstallTransaction::selectModelFiles(tree, QString(), QString(),
                                                  &models, mmproj);
        // 110 (group) beats 80 (single); parts are ordered by split index.
        QCOMPARE(models.size(), 2);
        QCOMPARE(models.first(), QStringLiteral("m/model-00001-of-00002.gguf"));
        QCOMPARE(models.last(), QStringLiteral("m/model-00002-of-00002.gguf"));
    }

    void selectPicksPreferredGroupAndMmproj()
    {
        const QList<HfFile> tree = {
            file(QStringLiteral("m/model-00001-of-00002.gguf"), 50),
            file(QStringLiteral("m/model-00002-of-00002.gguf"), 60),
            file(QStringLiteral("m/other-00001-of-00002.gguf"), 500),
            file(QStringLiteral("m/other-00002-of-00002.gguf"), 500),
            file(QStringLiteral("m/mmproj-model.gguf"), 5),
        };
        QStringList models;
        QString mmproj;
        ModelInstallTransaction::selectModelFiles(
            tree, QStringLiteral("model-00001-of-00002.gguf"),
            QStringLiteral("mmproj-model.gguf"), &models, mmproj);
        QCOMPARE(models.size(), 2);
        QVERIFY(models.contains(QStringLiteral("m/model-00001-of-00002.gguf")));
        QVERIFY(models.contains(QStringLiteral("m/model-00002-of-00002.gguf")));
        QCOMPARE(mmproj, QStringLiteral("m/mmproj-model.gguf"));
    }

    void selectSkipsDirsAndDefaultsToFirstProjector()
    {
        const QList<HfFile> tree = {
            file(QStringLiteral("dir"), 0, /*isDir=*/true),
            file(QStringLiteral("d/model.gguf"), 1),
            file(QStringLiteral("d/mmproj-b.gguf"), 1),
            file(QStringLiteral("d/mmproj-a.gguf"), 1),
        };
        QStringList models;
        QString mmproj;
        ModelInstallTransaction::selectModelFiles(tree, QString(), QString(),
                                                  &models, mmproj);
        QCOMPARE(models.size(), 1);
        QCOMPARE(models.first(), QStringLiteral("d/model.gguf"));
        // No preference: the first projector in tree order wins.
        QCOMPARE(mmproj, QStringLiteral("d/mmproj-b.gguf"));
    }

    void cancelAndShutdownAreSafeWhenIdle()
    {
        SettingsStore settings;
        settings.setRuntimeRootDir(m_dir.filePath(QStringLiteral("runtime")));
        settings.setRuntimeModelsDir(m_dir.filePath(QStringLiteral("models")));

        const QString presetsPath =
            QDir(m_dir.path()).filePath(QStringLiteral("launch-presets.json"));
        {
            QFile f(presetsPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArrayLiteral("{ \"schemaVersion\": 1, \"profiles\": [] }"));
        }
        LaunchProfileStore launchProfiles(settings, presetsPath);

        ModelInstallTransaction tx(settings, launchProfiles);
        QCOMPARE(tx.state(), ModelInstallTransaction::State::Idle);
        QVERIFY(!tx.busy());

        tx.cancel();
        QCOMPARE(tx.state(), ModelInstallTransaction::State::Idle);
        QVERIFY(!tx.busy());

        // installPrepared() without a prepared plan reports an error, not a crash.
        tx.installPrepared();
        QCOMPARE(tx.state(), ModelInstallTransaction::State::Error);
        QVERIFY(!tx.statusMessage().isEmpty());

        tx.shutdown();
    }
};

QTEST_MAIN(TestModelInstallTransaction)
#include "test_model_install_transaction.moc"
