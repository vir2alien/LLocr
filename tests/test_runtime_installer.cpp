// The runtime installer: the wizard's check step asks for the runtime
// without ever looking at the release list. The installer has to fetch the
// list itself — otherwise the download refuses to start with "No release
// selected — check for updates first", and the wizard's check step fails on
// a fresh profile where nothing was ever fetched.
//
// The channel tests cover the stable/beta split: per-channel caches under the
// app cache, the switch that refetches, the beta fallback when the stable
// chain fails, and the update flag across both channels.

#include <functional>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
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

    // The asset the installer's default backend will pick on this machine.
    static QString platformAssetName(const PlatformInfo &info, int build)
    {
        const QString b = QString::number(build);
        if (info.osTag == QLatin1String("win"))
            return QStringLiteral("llama-b%1-bin-win-%2-x64.zip").arg(b, info.backend);
        if (info.osTag == QLatin1String("macos"))
            return QStringLiteral("llama-b%1-bin-macos-%2.tar.gz").arg(b, info.arch);
        return QStringLiteral("llama-b%1-bin-ubuntu-%2.tar.gz").arg(b, info.arch);
    }

    static QJsonObject betaReleaseJson(const PlatformInfo &info, int build)
    {
        const QString name = platformAssetName(info, build);
        QJsonObject asset;
        asset.insert(QStringLiteral("name"), name);
        asset.insert(QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/%1").arg(name));
        asset.insert(QStringLiteral("size"), 1024);

        QJsonObject release;
        release.insert(QStringLiteral("tag_name"), QStringLiteral("b%1").arg(build));
        release.insert(QStringLiteral("published_at"), QStringLiteral("2025-06-01T00:00:00Z"));
        release.insert(QStringLiteral("prerelease"), true);
        release.insert(QStringLiteral("body"), QString());
        release.insert(QStringLiteral("assets"), QJsonArray{asset});
        return release;
    }

    // The composed stable release: a v-tag with no build number of its own —
    // the build comes back through the asset name — and no prerelease mark.
    static QJsonObject stableReleaseJson(const PlatformInfo &info, int build)
    {
        const QString name = platformAssetName(info, build);
        QJsonObject asset;
        asset.insert(QStringLiteral("name"), name);
        asset.insert(QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/%1").arg(name));
        asset.insert(QStringLiteral("size"), 2048);
        asset.insert(QStringLiteral("digest"), QStringLiteral("sha256:%1").arg(QString(64, QLatin1Char('a'))));

        QJsonObject release;
        release.insert(QStringLiteral("tag_name"), QStringLiteral("v1.0.0"));
        release.insert(QStringLiteral("name"), QStringLiteral("llama.cpp v1.0.0"));
        release.insert(QStringLiteral("published_at"), QStringLiteral("2025-11-01T00:00:00Z"));
        release.insert(QStringLiteral("prerelease"), false);
        release.insert(QStringLiteral("body"), QString());
        release.insert(QStringLiteral("assets"), QJsonArray{asset});
        return release;
    }

    static void writeReleasesCache(const QString &cacheDir, const QString &sourceUrl, const QJsonObject &release)
    {
        QVERIFY(QDir().mkpath(cacheDir));
        QFile file(QDir(cacheDir).filePath(QStringLiteral("releases.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QJsonObject cache;
        cache.insert(QStringLiteral("sourceUrl"), sourceUrl);
        cache.insert(QStringLiteral("releases"), QJsonArray{release});
        file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
    }

    // The release list the fetch reads instead of the network: the cache file
    // is fresh, so the fetch never leaves the machine. `sourceUrl` has to name
    // the URL the fetch binds its cache to.
    static void writeCachedReleases(const QString &root, const QString &sourceUrl = QString::fromUtf8(ReleaseCatalog::kApiUrl))
    {
        writeReleasesCache(QDir(root).filePath(QStringLiteral("app/cache/releases-beta")), sourceUrl, betaReleaseJson(ReleaseCatalog::detectPlatform(), 9999));
    }

    static void writeCachedStableRelease(const QString &root)
    {
        writeReleasesCache(QDir(root).filePath(QStringLiteral("app/cache/releases-stable")),
                           QString::fromUtf8(ReleaseCatalog::kStableApiBase) + QStringLiteral("/releases/latest"),
                           stableReleaseJson(ReleaseCatalog::detectPlatform(), 12359));
    }

    static bool waitFor(const std::function<bool()> &done, int timeoutMs = 20000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (done())
                return true;
            QTest::qWait(50);
        }
        return done();
    }

private:
    // Single-response/routed clone of the catalog tests' StatusServer: answers
    // every request with one canned status line so the fetch paths can be
    // exercised without touching the real GitHub API; setRoute() lets a test
    // override the canned answer by request path.
    class StatusServer : public QObject
    {
    public:
        bool start(const QByteArray &statusLine)
        {
            m_status = statusLine;
            connect(&m_server, &QTcpServer::newConnection, this, [this]() {
                while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                        const QByteArray request = socket->readAll();
                        if (!request.contains("\r\n\r\n"))
                            return;
                        socket->write(responseFor(request));
                        socket->flush();
                        socket->disconnectFromHost();
                    });
                    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                }
            });
            return m_server.listen(QHostAddress::LocalHost, 0);
        }

        void setRoute(const QString &pathSuffix, const QByteArray &response) { m_routes.insert(pathSuffix, response); }

        QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

        void stop() { m_server.close(); }

    private:
        QByteArray responseFor(const QByteArray &request) const
        {
            const int lineEnd = request.indexOf("\r\n");
            QByteArray path = request.left(lineEnd >= 0 ? lineEnd : request.size()).split(' ').value(1);
            const int query = path.indexOf('?');
            if (query >= 0)
                path.truncate(query);
            for (auto it = m_routes.constBegin(); it != m_routes.constEnd(); ++it) {
                if (path.endsWith(it.key().toUtf8()))
                    return it.value();
            }
            return m_status;
        }

        QByteArray m_status;
        QHash<QString, QByteArray> m_routes;
        QTcpServer m_server;
    };

private slots:
    // The install asked for before the release list was fetched must not be
    // refused: the installer fetches the list — here the beta channel's cache —
    // and installs with it.
    void theInstallFetchesTheReleaseListItNeeds()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        settings.setRuntimeChannel(QStringLiteral("beta"));
        writeCachedReleases(root.path());

        InstalledState installed(settings);
        RuntimeInstaller installer(settings, installed);

        QList<int> seen;
        connect(&installer, &RuntimeInstaller::stateChanged, this, [&]() { seen.append(installer.stateInt()); });

        installer.startDownloadAndInstall();

        // The refusal is gone: the installer goes for the release list.
        QCOMPARE(installer.stateInt(), RuntimeInstaller::State::Fetching);
        QVERIFY2(!installer.statusMessage().contains(QStringLiteral("No release selected")), qPrintable(installer.statusMessage()));

        // The list arrives, and the install starts with it.
        QVERIFY2(waitFor([&] { return seen.contains(RuntimeInstaller::State::Ready); }), "the release list never arrived");
        QVERIFY2(waitFor([&] { return seen.contains(RuntimeInstaller::State::Downloading); }), qPrintable(QStringLiteral("the download never started: %1").arg(installer.statusMessage())));

        installer.shutdown();
    }

    // The stable channel serves its own cache: the composed v-tag release with
    // the resolved build in the label, then the install starts from it.
    void stableChannelInstallsFromCachedStableRelease()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        // The isolation INI is shared by the slots of this process: the channel
        // is set explicitly, never inherited from another slot.
        settings.setRuntimeChannel(QStringLiteral("stable"));
        writeCachedStableRelease(root.path());

        InstalledState installed(settings);
        RuntimeInstaller installer(settings, installed);

        QList<int> seen;
        connect(&installer, &RuntimeInstaller::stateChanged, this, [&]() { seen.append(installer.stateInt()); });

        installer.startDownloadAndInstall();

        QVERIFY2(waitFor([&] { return seen.contains(RuntimeInstaller::State::Ready); }), "the stable catalog never arrived");
        QCOMPARE(installer.releaseCount(), 1);
        QVERIFY2(installer.releaseLabel(0).contains(QStringLiteral("v1.0.0")), qPrintable(installer.releaseLabel(0)));
        QVERIFY2(installer.releaseLabel(0).contains(QStringLiteral("(b12359)")), qPrintable(installer.releaseLabel(0)));
        QVERIFY2(waitFor([&] { return seen.contains(RuntimeInstaller::State::Downloading); }), qPrintable(QStringLiteral("the download never started: %1").arg(installer.statusMessage())));

        installer.shutdown();
    }

    // The channel switch saves the setting and refetches: the label flips
    // between the stable v-tag and the beta build, and back.
    void channelSwitchRefetches()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        settings.setRuntimeChannel(QStringLiteral("stable"));
        writeCachedStableRelease(root.path());
        writeCachedReleases(root.path());

        InstalledState installed(settings);
        RuntimeInstaller installer(settings, installed);

        installer.checkForUpdates();
        QVERIFY2(waitFor([&] { return installer.releaseLabel(0).contains(QStringLiteral("v1.0.0")); }), "the stable catalog never arrived");

        installer.setProperty("channel", QStringLiteral("beta"));
        QVERIFY2(waitFor([&] { return installer.releaseLabel(0).contains(QStringLiteral("b9999")); }), "the beta catalog never arrived after the switch");
        QCOMPARE(settings.runtimeChannel(), QStringLiteral("beta"));

        installer.setProperty("channel", QStringLiteral("stable"));
        QVERIFY2(waitFor([&] { return installer.releaseLabel(0).contains(QStringLiteral("v1.0.0")); }), "the stable catalog never came back");
        QCOMPARE(settings.runtimeChannel(), QStringLiteral("stable"));

        installer.shutdown();
    }

    // The stable chain failing (here: the API answers 500) must not leave the
    // installer empty: the cached beta list takes over, with the reason in the
    // status message.
    void stableChannelFallsBackToCachedBeta()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        StatusServer server;
        QVERIFY(server.start(QByteArrayLiteral("HTTP/1.1 500 Internal Server Error")));
        const QString base = server.baseUrl();

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        settings.setRuntimeChannel(QStringLiteral("stable"));
        // The fallback fetch binds its cache to the URL it asked for — the
        // mock's base here — so the cache must carry exactly that mark.
        writeCachedReleases(root.path(), base + QStringLiteral("/releases?per_page=10"));

        InstalledState installed(settings);
        RuntimeInstaller installer(settings, installed, nullptr, base);

        QList<int> seen;
        QString readyStatus;
        connect(&installer, &RuntimeInstaller::stateChanged, this, [&]() {
            seen.append(installer.stateInt());
            if (installer.stateInt() == RuntimeInstaller::State::Ready)
                readyStatus = installer.statusMessage();
        });

        installer.startDownloadAndInstall();

        QVERIFY2(waitFor([&] { return seen.contains(RuntimeInstaller::State::Ready); }), "the fallback catalog never arrived");
        // The install races on past Ready — the message is snapshot at the
        // transition, before the "Downloading …" line replaces the note.
        QVERIFY2(readyStatus.contains(QStringLiteral("beta")), qPrintable(readyStatus));
        QCOMPARE(installer.releaseCount(), 1);
        QVERIFY2(installer.releaseLabel(0).contains(QStringLiteral("b9999")), qPrintable(installer.releaseLabel(0)));
        QVERIFY2(waitFor([&] { return seen.contains(RuntimeInstaller::State::Downloading); }), qPrintable(QStringLiteral("the download never started: %1").arg(installer.statusMessage())));

        installer.shutdown();
        server.stop();
    }

    // The update flag compares the catalog's newest build against the active
    // one, whatever channel served the catalog.
    void hasUpdateAcrossChannels()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        settings.setRuntimeChannel(QStringLiteral("stable"));
        writeCachedStableRelease(root.path());
        writeCachedReleases(root.path());

        InstalledState installed(settings);
        RuntimeInstaller installer(settings, installed);

        int loads = 0;
        connect(&installer, &RuntimeInstaller::catalogChanged, this, [&]() { ++loads; });

        settings.setInstalledBuild(QStringLiteral("b12400"));
        installer.checkForUpdates();
        QVERIFY2(waitFor([&] { return installer.releaseLabel(0).contains(QStringLiteral("v1.0.0")); }), "the stable catalog never arrived");
        QVERIFY2(!installer.hasUpdate(), "v1.0.0 resolves to b12359 — not newer than the installed b12400");

        settings.setInstalledBuild(QStringLiteral("b100"));
        const int afterFirst = loads;
        installer.checkForUpdates();
        QVERIFY2(waitFor([&] { return loads > afterFirst; }), "the catalog never reloaded");
        QVERIFY2(installer.hasUpdate(), "b100 is older than the resolved b12359");

        installer.setProperty("channel", QStringLiteral("beta"));
        QVERIFY2(waitFor([&] { return installer.releaseLabel(0).contains(QStringLiteral("b9999")); }), "the beta catalog never arrived after the switch");

        settings.setInstalledBuild(QStringLiteral("b12400"));
        const int afterSwitch = loads;
        installer.checkForUpdates();
        QVERIFY2(waitFor([&] { return loads > afterSwitch; }), "the catalog never reloaded");
        QVERIFY2(!installer.hasUpdate(), "the beta b9999 is not newer than the installed b12400");

        settings.setInstalledBuild(QStringLiteral("b100"));
        const int last = loads;
        installer.checkForUpdates();
        QVERIFY2(waitFor([&] { return loads > last; }), "the catalog never reloaded");
        QVERIFY2(installer.hasUpdate(), "b100 is older than the beta b9999");

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
