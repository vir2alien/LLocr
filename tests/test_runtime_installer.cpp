// The runtime installer: the wizard's check step asks for the runtime
// without ever looking at the release list. The installer has to fetch the
// list itself — otherwise the download refuses to start with "No release
// selected — check for updates first", and the wizard's check step fails on
// a fresh profile where nothing was ever fetched.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "config/SettingsStore.h"
#include "runtime/InstalledState.h"
#include "runtime/ReleaseCatalog.h"
#include "runtime/RuntimeInstaller.h"
#include "testsettings.h"

using namespace llocr;

class TestRuntimeInstaller : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

    static void pointAtTempDir(SettingsStore &settings, const QString &root)
    {
        settings.setRuntimeRootDir(QDir(root).filePath(QStringLiteral("app")));
        settings.setRuntimeModelsDir(QDir(root).filePath(QStringLiteral("models")));
        settings.setInstalledBuild(QString());
    }

    // The release list the fetch reads instead of the network: the cache file
    // is fresh, so the fetch never leaves the machine.
    static void writeCachedReleases(const QString &cacheDir)
    {
        QVERIFY(QDir().mkpath(cacheDir));

        const PlatformInfo info = ReleaseCatalog::detectPlatform();
        QString fileName;
        if (info.osTag == QLatin1String("win"))
            fileName = QStringLiteral("llama-b9999-bin-win-cpu-x64.zip");
        else if (info.osTag == QLatin1String("macos"))
            fileName = QStringLiteral("llama-b9999-bin-macos-%1.tar.gz").arg(info.arch);
        else
            fileName = QStringLiteral("llama-b9999-bin-ubuntu-%1.tar.gz").arg(info.arch);

        QJsonObject asset;
        asset.insert(QStringLiteral("name"), fileName);
        asset.insert(QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/%1").arg(fileName));
        asset.insert(QStringLiteral("size"), 1024);

        QJsonObject release;
        release.insert(QStringLiteral("tag_name"), QStringLiteral("b9999"));
        release.insert(QStringLiteral("body"), QString());
        release.insert(QStringLiteral("assets"), QJsonArray{asset});

        QFile file(QDir(cacheDir).filePath(QStringLiteral("releases.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QJsonObject cache;
        cache.insert(QStringLiteral("sourceUrl"), QLatin1String(ReleaseCatalog::kApiUrl));
        cache.insert(QStringLiteral("releases"), QJsonArray{release});
        file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
        file.close();
    }

private slots:
    // The install asked for before the release list was fetched must not be
    // refused: the installer fetches the list and installs with it.
    void theInstallFetchesTheReleaseListItNeeds()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        writeCachedReleases(QDir(root.path()).filePath(QStringLiteral("app/cache")));

        InstalledState installed(settings);
        RuntimeInstaller installer(settings, installed);

        QList<int> seen;
        connect(&installer, &RuntimeInstaller::stateChanged, this, [&]() { seen.append(installer.stateInt()); });

        installer.startDownloadAndInstall();

        // The refusal is gone: the installer goes for the release list.
        QCOMPARE(installer.stateInt(), RuntimeInstaller::State::Fetching);
        QVERIFY2(!installer.statusMessage().contains(QStringLiteral("No release selected")), qPrintable(installer.statusMessage()));

        // The list arrives, and the install starts with it.
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000 && !seen.contains(RuntimeInstaller::State::Downloading))
            QTest::qWait(50);
        QVERIFY2(seen.contains(RuntimeInstaller::State::Ready), "the release list never arrived");
        QVERIFY2(seen.contains(RuntimeInstaller::State::Downloading), "the download never started");

        installer.shutdown();
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_runtime_installer"));
    TestRuntimeInstaller test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_runtime_installer.moc"
