#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "app/AppController.h"
#include "app/OcrImageProvider.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("llocr"));
    QCoreApplication::setApplicationName(QStringLiteral("LLM OCR"));

    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    llocr::AppController controller;

    controller.loadSettings();

    QQmlApplicationEngine engine;

    // The image provider is owned by the engine after addImageProvider().
    engine.addImageProvider(QStringLiteral("ocr"),
                            new llocr::OcrImageProvider(&controller));

    // Expose the single backend object to QML. The box overlay model is
    // reached through `controller.boxModel`, so no extra context property is
    // needed (and would only duplicate/desynchronize the overlay state).
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
