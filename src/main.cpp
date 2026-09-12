#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "app/AppController.h"
#include "app/I18n.h"
#include "app/LaunchProfileStore.h"
#include "app/OcrImageProvider.h"
#include "app/RequestProfileStore.h"
#include "app/SettingsStore.h"
#include "app/UiController.h"
#include "runtime/InstallTransaction.h"
#include "runtime/ModelInstaller.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeInstaller.h"
#include "runtime/RuntimeLog.h"
#include "runtime/RuntimePaths.h"
#include "runtime/SelfTestController.h"
#include "runtime/SingleInstanceGuard.h"

namespace {

void setApplicationIdentity() {
    QCoreApplication::setOrganizationName(QStringLiteral("llocr"));
    QCoreApplication::setApplicationName(QStringLiteral("LLM OCR"));
}

QIcon makeWindowIcon() {
    QIcon windowIcon;
    for (const auto size : {16, 24, 32, 48, 64, 128, 256, 512, 1024})
        windowIcon.addFile(QStringLiteral(":/icons/llocr-%1.png").arg(size),
                           QSize(size, size));
    return windowIcon;
}

void applyVisualStyle(QGuiApplication& app) {
    app.setWindowIcon(makeWindowIcon());
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
}

void prepareRuntimeDirectories(const llocr::RuntimePaths& paths) {
    const QString dirError = paths.ensureDirectories();
    if (!dirError.isEmpty())
        qWarning().noquote() << dirError;

    llocr::InstallTransaction::cleanupStaging(paths);
}

void connectShutdownHandlers(QGuiApplication& app,
                             llocr::SingleInstanceGuard& instanceGuard,
                             llocr::RuntimeController& runtimeController,
                             llocr::RuntimeInstaller& runtimeInstaller,
                             llocr::ModelInstaller& modelInstaller) {
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &instanceGuard,
                     &llocr::SingleInstanceGuard::release);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtimeController,
                     &llocr::RuntimeController::shutdownSync);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtimeInstaller,
                     &llocr::RuntimeInstaller::shutdown);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &modelInstaller,
                     &llocr::ModelInstaller::shutdown);
}

void setupQmlEngine(QQmlApplicationEngine& engine,
                    llocr::AppController& appController,
                    llocr::UiController& uiController) {
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/Theme.qml")),
                             "LLocr", 1, 0, "Theme");
    qmlRegisterUncreatableType<llocr::UiController>(
                "LLocr", 1, 0, "UiController",
                "UiController is provided as a context property");

    engine.addImageProvider(QStringLiteral("ocr"),
                            new llocr::OcrImageProvider(&appController));

    engine.rootContext()->setContextProperty(QStringLiteral("controller"),
                                             &appController);
    engine.rootContext()->setContextProperty(QStringLiteral("uiController"),
                                             &uiController);
}

}  // namespace

int main(int argc, char* argv[]) {
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    setApplicationIdentity();
    applyVisualStyle(app);

    llocr::SettingsStore settingsStore;
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", &settingsStore);

    llocr::I18n i18n(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "I18n", &i18n);

    llocr::RequestProfileStore requestProfiles(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RequestProfiles", &requestProfiles);

    llocr::LaunchProfileStore launchProfiles(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "LaunchProfiles", &launchProfiles);

    llocr::RuntimeController runtimeController(settingsStore, launchProfiles);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", &runtimeController);

    llocr::RuntimeLog runtimeLog(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RuntimeLog", &runtimeLog);
    runtimeController.setLogTarget(&runtimeLog);

    llocr::SelfTestController selfTestController(settingsStore, runtimeController,
                                                 requestProfiles);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "SelfTest", &selfTestController);

    llocr::RuntimeInstaller runtimeInstaller(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RuntimeInstaller", &runtimeInstaller);

    llocr::ModelInstaller modelInstaller(settingsStore, runtimeController,
                                         launchProfiles);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "ModelInstaller", &modelInstaller);

    llocr::RuntimePaths runtimePaths(settingsStore.runtimeRootDir(),
                                     settingsStore.runtimeModelsDir());
    prepareRuntimeDirectories(runtimePaths);

    llocr::SingleInstanceGuard instanceGuard(runtimePaths.instanceLockPath());
    QString guardError;
    const bool soleInstance = instanceGuard.tryAcquire(guardError);
    runtimeController.setSingleInstanceHeld(!soleInstance);
    runtimeController.bindSingleInstanceGuard(&instanceGuard);
    if (!soleInstance)
        qWarning().noquote() << guardError;

    connectShutdownHandlers(app, instanceGuard, runtimeController,
                            runtimeInstaller, modelInstaller);

    QQmlApplicationEngine engine;

    QObject::connect(&i18n, &llocr::I18n::languageApplied, &engine,
                     [&engine]() { engine.retranslate(); });

    llocr::AppController appController(settingsStore, runtimeController,
                                       requestProfiles);
    llocr::UiController uiController(settingsStore);

    setupQmlEngine(engine, appController, uiController);
    i18n.applyInitial();

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
