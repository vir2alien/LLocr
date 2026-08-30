#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "app/AppController.h"
#include "app/I18n.h"
#include "app/OcrImageProvider.h"
#include "app/SettingsStore.h"
#include "app/UiController.h"
#include "runtime/InstallTransaction.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeInstaller.h"
#include "runtime/RuntimeLog.h"
#include "runtime/SelfTestController.h"
#include "runtime/ModelInstaller.h"
#include "runtime/RuntimePaths.h"
#include "runtime/SingleInstanceGuard.h"

int main(int argc, char* argv[]) {
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("llocr"));
    QCoreApplication::setApplicationName(QStringLiteral("LLM OCR"));

    // Application / window icon, used by every top-level window (Windows
    // taskbar, Linux WM/taskbar, and macOS window). Emits each size so the
    // platform can pick the crispest available variant for the current scale
    // factor. (The macOS Dock icon is additionally supplied by the .icns in the
    // .app bundle; see the LLOCR_MACOS_APP_BUNDLE build option.)
    {
        QIcon windowIcon;
        for (const auto size : {16, 24, 32, 48, 64, 128, 256, 512, 1024})
            windowIcon.addFile(QStringLiteral(":/icons/llocr-%1.png").arg(size),
                               QSize(size, size));
        app.setWindowIcon(windowIcon);
    }

    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    llocr::SettingsStore settingsStore;
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", &settingsStore);
    llocr::I18n i18n(settingsStore);

    // Managed-runtime controller: single instance, owned here (ADR 36).
    // Created before the engine loads; QML only consumes the singleton.
    llocr::RuntimeController runtimeController(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", &runtimeController);

    // § review 3.4: the managed server's live log moved out of the facade into
    // a dedicated singleton; RuntimeController pushes servers into it.
    llocr::RuntimeLog runtimeLog(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RuntimeLog", &runtimeLog);
    runtimeController.setLogTarget(&runtimeLog);

    // § review 3.4: self-test state + wizard "Check" bridge moved out of the
    // facade into a dedicated singleton that consumes Runtime's resolve API.
    llocr::SelfTestController selfTestController(settingsStore, runtimeController);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "SelfTest", &selfTestController);

    // Stage D install controller: release catalog, download & install. Also a
    // singleton; owns its own DownloadManager and worker threads.
    llocr::RuntimeInstaller runtimeInstaller(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RuntimeInstaller", &runtimeInstaller);

    // Stage E model management: preset catalog, HF search/download, registry.
    llocr::ModelInstaller modelInstaller(settingsStore, runtimeController);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "ModelInstaller", &modelInstaller);

    // Single-instance guard (§ Stage A task 7): when another instance holds the
    // lock, Managed operations are disabled via runtimeController.setSingleInstanceHeld().
    llocr::RuntimePaths paths(settingsStore.runtimeRootDir(),
                              settingsStore.runtimeModelsDir());
    // Make sure <AppData>/LLocr exists before QLockFile tries to create the
    // instance lock in it. Failures here are non-fatal: the lock file just
    // cannot be created and External keeps working.
    const QString dirError = paths.ensureDirectories();
    if (!dirError.isEmpty())
        qWarning().noquote() << dirError;

    // ADR 39 / Stage D task 5: remove leftover staging/<uuid> trees from
    // interrupted installs. Safe now that cleanupStaging skips "."/"..".
    llocr::InstallTransaction::cleanupStaging(paths);

    llocr::SingleInstanceGuard instanceGuard(paths.instanceLockPath());
    QString guardError;
    const bool soleInstance = instanceGuard.tryAcquire(guardError);
    runtimeController.setSingleInstanceHeld(!soleInstance);
    if (!soleInstance) {
        qWarning().noquote() << guardError;
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &instanceGuard,
                     &llocr::SingleInstanceGuard::release);
    // §5.5: blocking shutdown of the managed server (terminate → 5 s → kill).
    // Must run while the process is still alive; ~aboutToQuit is the last
    // synchronous point before the event loop stops.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtimeController,
                     &llocr::RuntimeController::shutdownSync);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtimeInstaller,
                     &llocr::RuntimeInstaller::shutdown);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &modelInstaller,
                     &llocr::ModelInstaller::shutdown);

    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("LLocr", 1, 0, "I18n", &i18n);

    QObject::connect(&i18n, &llocr::I18n::languageApplied, &engine,
                     [&engine]() { engine.retranslate(); });

    llocr::AppController appController(settingsStore, runtimeController);
    llocr::UiController uiController(settingsStore);

    qmlRegisterSingletonType(QUrl("qrc:/qml/Theme.qml"), "LLocr", 1, 0, "Theme");

    qmlRegisterUncreatableType<llocr::UiController>("LLocr", 1, 0, "UiController", "UiController is provided as a context property");

    engine.addImageProvider(QStringLiteral("ocr"), new llocr::OcrImageProvider(&appController));

    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &appController);
    engine.rootContext()->setContextProperty(QStringLiteral("uiController"), &uiController);

    i18n.applyInitial();

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
