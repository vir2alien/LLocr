#include <QDir>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>

#include "app/SettingsStore.h"
#include "runtime/RuntimeInstaller.h"
#include "runtime/RuntimePaths.h"

using namespace llocr;

// §H.6 — separate install lock.
//
// Runtime installs (download + install + cleanup) are guarded by a dedicated
// `.install.lock` (QLockFile) that is *independent* of the app instance lock,
// so a second GUI instance can keep using External / browsing while a runtime
// install stays exclusive. These tests pin down the lock path and the
// refusal/success behaviour of the guarded entry point.
class TestInstallLock : public QObject {
    Q_OBJECT

private:
    static RuntimePaths makePaths(const QString &root)
    {
        return RuntimePaths(QDir(root).filePath(QStringLiteral("app")),
                            QDir(root).filePath(QStringLiteral("models")));
    }

    static void pointAtTempDir(SettingsStore &settings, const QString &root)
    {
        settings.setRuntimeRootDir(QDir(root).filePath(QStringLiteral("app")));
        settings.setRuntimeModelsDir(QDir(root).filePath(QStringLiteral("models")));
        settings.setInstalledBuild(QString());
    }

private slots:
    void installLockPathSitsInsideRuntimeDir()
    {
        QTemporaryDir root;
        const RuntimePaths paths = makePaths(root.path());
        QCOMPARE(paths.installLockPath(),
                 QDir(paths.runtimeDir()).filePath(QStringLiteral(".install.lock")));
        QVERIFY(paths.installLockPath().endsWith(QStringLiteral(".install.lock")));
        QVERIFY(paths.installLockPath().startsWith(paths.runtimeDir()));
    }

    void cleanupIsRefusedWhileAnotherInstanceInstalls()
    {
        QTemporaryDir root;
        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        RuntimeInstaller installer(settings);

        // Instance 1 is mid-install: it holds the install lock.
        const RuntimePaths paths = makePaths(root.path());
        QLockFile other(paths.installLockPath());
        QVERIFY(other.tryLock(0));

        const QString refused = installer.cleanupUnusedBuilds();
        QVERIFY2(refused.contains(QStringLiteral("installing"), Qt::CaseInsensitive),
                 qPrintable(refused));

        // Once instance 1 releases the lock, the same op succeeds (no refusal).
        other.unlock();
        const QString summary = installer.cleanupUnusedBuilds();
        QVERIFY2(!summary.contains(QStringLiteral("installing"), Qt::CaseInsensitive),
                 qPrintable(summary));
    }

    void secondInstallLockFileCannotInterleave()
    {
        QTemporaryDir root;
        const RuntimePaths paths = makePaths(root.path());
        QDir().mkpath(paths.runtimeDir());

        QLockFile a(paths.installLockPath());
        QVERIFY(a.tryLock(0));
        QLockFile b(paths.installLockPath());
        QVERIFY(!b.tryLock(0));   // a holds it — b is refused
        a.unlock();
        QVERIFY(b.tryLock(0));    // after release b can take it
        b.unlock();
    }
};

QTEST_MAIN(TestInstallLock)
#include "test_install_lock.moc"