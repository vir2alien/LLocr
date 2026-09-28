#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "app/AppController.h"
#include "app/I18n.h"
#include "app/OcrImageProvider.h"
#include "app/ProblemLog.h"
#include "app/UiController.h"
#include "app/VerificationPromptStore.h"
#include "config/RequestProfileStore.h"
#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "core/ModelProfiles.h"
#include "runtime/InstalledState.h"
#include "runtime/InstallTransaction.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/ModelInstaller.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeInstaller.h"
#include "runtime/RuntimeLog.h"
#include "runtime/SelfTestController.h"
#include "runtime/SingleInstanceGuard.h"

namespace {

void setApplicationIdentity()
{
    QCoreApplication::setOrganizationName(QStringLiteral("llocr"));
    QCoreApplication::setApplicationName(QStringLiteral("LLM OCR"));
}

QIcon makeWindowIcon()
{
    QIcon windowIcon;
    for (const auto size : {16, 24, 32, 48, 64, 128, 256, 512, 1024})
        windowIcon.addFile(QStringLiteral(":/icons/llocr-%1.png").arg(size), QSize(size, size));
    return windowIcon;
}

void applyVisualStyle(QGuiApplication &app)
{
    app.setWindowIcon(makeWindowIcon());
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
}

void prepareRuntimeDirectories(const llocr::RuntimePaths &paths)
{
    const QString dirError = paths.ensureDirectories();
    if (!dirError.isEmpty())
        qWarning().noquote() << dirError;

    llocr::InstallTransaction::cleanupStaging(paths);
}

void connectShutdownHandlers(
    QGuiApplication &app, llocr::SingleInstanceGuard &instanceGuard, llocr::RuntimeController &runtimeController, llocr::RuntimeInstaller &runtimeInstaller, llocr::ModelInstaller &modelInstaller)
{
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &instanceGuard, &llocr::SingleInstanceGuard::release);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtimeController, &llocr::RuntimeController::shutdownSync);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtimeInstaller, &llocr::RuntimeInstaller::shutdown);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &modelInstaller, &llocr::ModelInstaller::shutdown);
}

void setupQmlEngine(QQmlApplicationEngine &engine, llocr::AppController &appController, llocr::UiController &uiController)
{
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Controller", &appController);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", &uiController);

    engine.addImageProvider(QStringLiteral("ocr"), new llocr::OcrImageProvider(&appController));
}

}  // namespace

int main(int argc, char *argv[])
{
    qInfo() << "runtime Qt:" << qVersion() << QLibraryInfo::path(QLibraryInfo::LibrariesPath);

    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    setApplicationIdentity();
    applyVisualStyle(app);

    llocr::SettingsStore settingsStore;
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", &settingsStore);

    llocr::I18n i18n(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "I18n", &i18n);

    const QList<llocr::ModelProfiles::Profile> modelProfiles = llocr::ModelProfiles::instance();

    llocr::RequestProfileStore requestProfilesOcr(settingsStore);
    requestProfilesOcr.setModelProfiles(modelProfiles);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RequestProfilesOcr", &requestProfilesOcr);

    llocr::RequestProfileStore requestProfilesValidate(settingsStore, llocr::RequestProfileStore::Role::Check);
    requestProfilesValidate.setModelProfiles(modelProfiles);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RequestProfilesValidate", &requestProfilesValidate);

    llocr::VerificationPromptStore verificationPrompts(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Verification", &verificationPrompts);

    llocr::LaunchProfileStore launchProfiles(settingsStore);
    launchProfiles.setModelProfiles(modelProfiles);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "LaunchProfiles", &launchProfiles);

    llocr::InstalledState installedState(settingsStore);

    llocr::RuntimeController runtimeController(settingsStore, launchProfiles, &installedState);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", &runtimeController);

    llocr::RuntimeLog runtimeLog(settingsStore);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RuntimeLog", &runtimeLog);
    runtimeController.setLogTarget(&runtimeLog);

    llocr::SelfTestController selfTestController(settingsStore, runtimeController, requestProfilesOcr);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "SelfTest", &selfTestController);

    llocr::RuntimeInstaller runtimeInstaller(settingsStore, installedState);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "RuntimeInstaller", &runtimeInstaller);

    llocr::ModelInstaller modelInstaller(settingsStore, runtimeController, installedState);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "ModelInstaller", &modelInstaller);

    llocr::RuntimePaths runtimePaths(settingsStore.runtimeRootDir(), settingsStore.runtimeModelsDir());
    prepareRuntimeDirectories(runtimePaths);

    llocr::SingleInstanceGuard instanceGuard(runtimePaths.instanceLockPath());
    QString guardError;
    const bool soleInstance = instanceGuard.tryAcquire(guardError);
    runtimeController.setSingleInstanceHeld(!soleInstance);
    runtimeController.bindSingleInstanceGuard(&instanceGuard);
    if (!soleInstance)
        qWarning().noquote() << guardError;

    connectShutdownHandlers(app, instanceGuard, runtimeController, runtimeInstaller, modelInstaller);

    llocr::AppController appController(settingsStore, runtimeController, requestProfilesOcr, requestProfilesValidate, verificationPrompts);
    llocr::ProblemLog problemLog;
    appController.setProblemLog(&problemLog);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Log", &problemLog);
    llocr::UiController uiController(settingsStore);

    QQmlApplicationEngine engine;

    QObject::connect(&i18n, &llocr::I18n::languageApplied, &engine, [&engine]() { engine.retranslate(); });
    QObject::connect(&i18n, &llocr::I18n::languageApplied, &runtimeController, &llocr::RuntimeController::retranslate);
    QObject::connect(&i18n, &llocr::I18n::languageApplied, &runtimeInstaller, &llocr::RuntimeInstaller::retranslate);
    QObject::connect(&i18n, &llocr::I18n::languageApplied, &modelInstaller, &llocr::ModelInstaller::retranslate);
    QObject::connect(&i18n, &llocr::I18n::languageApplied, &selfTestController, &llocr::SelfTestController::retranslate);
    QObject::connect(&i18n, &llocr::I18n::languageApplied, &appController, &llocr::AppController::retranslate);
    QObject::connect(&i18n, &llocr::I18n::languageApplied, &problemLog, &llocr::ProblemLog::retranslate);

    setupQmlEngine(engine, appController, uiController);
    i18n.applyInitial();

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/LLocr/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
