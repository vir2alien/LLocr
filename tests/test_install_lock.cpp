#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>
#include <QVariantMap>

#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "runtime/InstalledBuildsModel.h"
#include "runtime/InstalledState.h"
#include "runtime/RuntimeInstaller.h"
#include "testsettings.h"

using namespace llocr;

// §H.6 — separate install lock, plus the installed-builds scan/activation
// (settings-reset recovery: previously downloaded llama.cpp builds can be
// activated without a re-download).
//
// Runtime installs (download + install + cleanup) are guarded by a dedicated
// `.install.lock` (QLockFile) that is *independent* of the app instance lock,
// so a second GUI instance can keep using External / browsing while a runtime
// install stays exclusive. These tests pin down the lock path and the
// refusal/success behaviour of the guarded entry point.
class TestInstallLock : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

    static RuntimePaths makePaths(const QString &root) { return RuntimePaths(QDir(root).filePath(QStringLiteral("app")), QDir(root).filePath(QStringLiteral("models"))); }

    static void pointAtTempDir(SettingsStore &settings, const QString &root)
    {
        settings.setRuntimeRootDir(QDir(root).filePath(QStringLiteral("app")));
        settings.setRuntimeModelsDir(QDir(root).filePath(QStringLiteral("models")));
        settings.setInstalledBuild(QString());
    }

    // Creates a fake installed build: <runtimeDir>/llama.cpp-<build>-<backend>
    // -<os>-<arch>/ containing a llama-server stub (content is irrelevant for
    // the scan; only the file's existence matters).
    static void makeBuild(const QString &root, const QString &tag)
    {
        const RuntimePaths paths = makePaths(root);
        const QString dir = QDir(paths.runtimeDir()).filePath(tag);
        QDir().mkpath(dir);
        const QString binary =
#ifdef Q_OS_WIN
            QStringLiteral("llama-server.exe");
#else
            QStringLiteral("llama-server");
#endif
        QFile f(QDir(dir).filePath(binary));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("stub", 4) == 4);
    }

    // The installed builds are a real list model now (ADR 115); the helpers
    // read it the same way the QML delegates do.
    static QAbstractItemModel *buildsOf(RuntimeInstaller &installer) { return qobject_cast<QAbstractItemModel *>(installer.installedBuilds()); }

    static QVariant buildData(RuntimeInstaller &installer, int row, int role)
    {
        auto *model = buildsOf(installer);
        return model->data(model->index(row, 0), role);
    }

    static int findBuild(RuntimeInstaller &installer, const QString &tag)
    {
        auto *model = buildsOf(installer);
        for (int i = 0; i < model->rowCount(); ++i) {
            if (model->data(model->index(i, 0), InstalledBuildsModel::TagRole).toString() == tag)
                return i;
        }
        return -1;
    }

private slots:
    void installLockPathSitsInsideRuntimeDir()
    {
        QTemporaryDir root;
        const RuntimePaths paths = makePaths(root.path());
        QCOMPARE(paths.installLockPath(), QDir(paths.runtimeDir()).filePath(QStringLiteral(".install.lock")));
        QVERIFY(paths.installLockPath().endsWith(QStringLiteral(".install.lock")));
        QVERIFY(paths.installLockPath().startsWith(paths.runtimeDir()));
    }

    void cleanupIsRefusedWhileAnotherInstanceInstalls()
    {
        QTemporaryDir root;
        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        InstalledState installed(settings);
        RuntimeInstaller installer(settings, installed);

        // An active build must exist, otherwise cleanup is refused by the
        // no-active-build guard instead of taking the lock at all.
        makeBuild(root.path(), QStringLiteral("llama.cpp-b100-cpu-win-x64"));
        settings.setInstalledBuild(QStringLiteral("b100"));

        // Instance 1 is mid-install: it holds the install lock.
        const RuntimePaths paths = makePaths(root.path());
        QLockFile other(paths.installLockPath());
        QVERIFY(other.tryLock(0));

        const QString refused = installer.cleanupUnusedBuilds();
        QVERIFY2(refused.contains(QStringLiteral("installing"), Qt::CaseInsensitive), qPrintable(refused));

        // Once instance 1 releases the lock, the same op succeeds (no refusal).
        other.unlock();
        const QString summary = installer.cleanupUnusedBuilds();
        QVERIFY2(!summary.contains(QStringLiteral("installing"), Qt::CaseInsensitive), qPrintable(summary));
    }

    void secondInstallLockFileCannotInterleave()
    {
        QTemporaryDir root;
        const RuntimePaths paths = makePaths(root.path());
        QDir().mkpath(paths.runtimeDir());

        QLockFile a(paths.installLockPath());
        QVERIFY(a.tryLock(0));
        QLockFile b(paths.installLockPath());
        QVERIFY(!b.tryLock(0));
        a.unlock();
        QVERIFY(b.tryLock(0));
        b.unlock();
    }

    // The settings-reset scenario: the active-build keys are wiped, but the
    // downloaded builds remain on disk. The scan lists them, activation makes
    // one of them the managed runtime again, and cleanup is refused while no
    // build is active (it would otherwise delete every download).
    void scanAndActivateRecoverAfterSettingsReset()
    {
        QTemporaryDir root;
        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        InstalledState installed(settings);
        RuntimeInstaller installer(settings, installed);

        makeBuild(root.path(), QStringLiteral("llama.cpp-b100-cpu-win-x64"));
        makeBuild(root.path(), QStringLiteral("llama.cpp-b101-cuda-cu12-win-x64"));
        makeBuild(root.path(), QStringLiteral("llama.cpp-b99-cpu-win-x64"));
        // Non-install dirs must be ignored by the scan.
        QDir().mkpath(QDir(makePaths(root.path()).runtimeDir()).filePath(QStringLiteral("staging")));
        QDir().mkpath(QDir(makePaths(root.path()).runtimeDir()).filePath(QStringLiteral("not-a-build")));

        installer.rescanInstalledBuilds();
        QCOMPARE(installer.installedBuildCount(), 3);

        // Newest build first (b101), backend parsed from the tag including the
        // hyphenated "cuda-cu12" token.
        QCOMPARE(buildData(installer, 0, InstalledBuildsModel::BuildRole).toString(), QStringLiteral("b101"));
        QCOMPARE(buildData(installer, 0, InstalledBuildsModel::BackendRole).toString(), QStringLiteral("cuda-cu12"));
        QCOMPARE(buildData(installer, 0, InstalledBuildsModel::ActiveRole).toBool(), false);
        const QString newestPath = buildData(installer, 0, InstalledBuildsModel::ServerPathRole).toString();
        QVERIFY(!newestPath.isEmpty());

        // Activate it: the settings now point at the scanned binary.
        QVERIFY(installer.activateBuild(0).isEmpty());
        QCOMPARE(settings.serverPath(), newestPath);
        QCOMPARE(settings.installedBuild(), QStringLiteral("b101"));
        QCOMPARE(settings.runtimeBackend(), QStringLiteral("cuda-cu12"));
        QVERIFY(settings.serverPathIsManaged());
        // The active highlight follows the settings, not a stored flag.
        QCOMPARE(buildData(installer, 0, InstalledBuildsModel::ActiveRole).toBool(), true);

        // Other rows are not active, activating one switches cleanly.
        const int oldIdx = findBuild(installer, QStringLiteral("llama.cpp-b100-cpu-win-x64"));
        QVERIFY(oldIdx >= 0);
        QVERIFY(installer.activateBuild(oldIdx).isEmpty());
        QCOMPARE(settings.installedBuild(), QStringLiteral("b100"));
        QCOMPARE(settings.runtimeBackend(), QStringLiteral("cpu"));
        QCOMPARE(buildData(installer, 0, InstalledBuildsModel::ActiveRole).toBool(), false);
        QCOMPARE(buildData(installer, oldIdx, InstalledBuildsModel::ActiveRole).toBool(), true);

        // A build whose directory lost its binary cannot be activated.
        QDir(QDir(makePaths(root.path()).runtimeDir()).filePath(QStringLiteral("llama.cpp-b99-cpu-win-x64")))
#ifdef Q_OS_WIN
            .remove(QStringLiteral("llama-server.exe"));
#else
            .remove(QStringLiteral("llama-server"));
#endif
        installer.rescanInstalledBuilds();
        const int missingIdx = findBuild(installer, QStringLiteral("llama.cpp-b99-cpu-win-x64"));
        QVERIFY(missingIdx >= 0);
        QCOMPARE(buildData(installer, missingIdx, InstalledBuildsModel::BinaryFoundRole).toBool(), false);
        const QString serverPathBefore = settings.serverPath();
        QVERIFY(!installer.activateBuild(missingIdx).isEmpty());
        QCOMPARE(settings.serverPath(), serverPathBefore);

        // Cleanup keeps only the active build (b100); the others are swept.
        const QString summary = installer.cleanupUnusedBuilds();
        QVERIFY(summary.contains(QStringLiteral("Removed")));
        QVERIFY(QFileInfo::exists(QDir(makePaths(root.path()).runtimeDir()).filePath(QStringLiteral("llama.cpp-b100-cpu-win-x64"))));
        QVERIFY(!QFileInfo::exists(QDir(makePaths(root.path()).runtimeDir()).filePath(QStringLiteral("llama.cpp-b101-cuda-cu12-win-x64"))));
    }

    void cleanupRefusedWithoutActiveBuild()
    {
        QTemporaryDir root;
        SettingsStore settings;
        pointAtTempDir(settings, root.path());  // installedBuild left empty
        InstalledState installed(settings);
        RuntimeInstaller installer(settings, installed);

        makeBuild(root.path(), QStringLiteral("llama.cpp-b100-cpu-win-x64"));
        installer.rescanInstalledBuilds();
        QCOMPARE(installer.installedBuildCount(), 1);

        const QString refused = installer.cleanupUnusedBuilds();
        QVERIFY2(refused.contains(QStringLiteral("active"), Qt::CaseInsensitive), qPrintable(refused));
        // The downloaded build survived the refused sweep.
        QVERIFY(QFileInfo::exists(QDir(makePaths(root.path()).runtimeDir()).filePath(QStringLiteral("llama.cpp-b100-cpu-win-x64"))));
    }
};

QTEST_MAIN(TestInstallLock)
#include "test_install_lock.moc"