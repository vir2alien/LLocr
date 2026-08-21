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

    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("LLocr", 1, 0, "I18n", &i18n);

    QObject::connect(&i18n, &llocr::I18n::languageApplied, &engine,
                     [&engine]() { engine.retranslate(); });

    llocr::AppController appController(settingsStore);
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
