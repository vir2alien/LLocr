#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "app/AppController.h"
#include "app/OcrImageProvider.h"
#include "app/SettingsStore.h"
#include "app/UiController.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("llocr"));
    QCoreApplication::setApplicationName(QStringLiteral("LLM OCR"));

    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    llocr::SettingsStore settingsStore;
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", &settingsStore);
    llocr::AppController appController(settingsStore);
    llocr::UiController uiController(settingsStore);

    QQmlApplicationEngine engine;

    qmlRegisterSingletonType(QUrl("qrc:/qml/Theme.qml"), "LLocr", 1, 0, "ThemeSingleton");

    qmlRegisterUncreatableType<llocr::UiController>("LLocr", 1, 0, "UiController", "UiController is provided as a context property");

    engine.addImageProvider(QStringLiteral("ocr"), new llocr::OcrImageProvider(&appController));

    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &appController);
    engine.rootContext()->setContextProperty(QStringLiteral("uiController"), &uiController);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
