#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>

#include "app/SettingsStore.h"
#include "runtime/RuntimeController.h"

using namespace llocr;

// Verifies the §1.2 contract: QML's `Runtime` singleton must return exactly the
// instance passed to AppController (the one created in main.cpp). QML must not
// be able to create its own RuntimeController.
class TestRuntimeLifetime : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_runtime_lifetime"));
    }

    void qmlSingletonReturnsSameInstance()
    {
        SettingsStore settings;
        RuntimeController runtime(settings);

        QQmlEngine engine;
        qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", &runtime);

        QQmlComponent component(&engine);
        component.setData(
            QByteArray("import LLocr 1.0\n"
                       "import QtQml 2.0\n"
                       "QtObject { property var rt: Runtime }\n"),
            QUrl(QStringLiteral("qrc:/test/instance.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QScopedPointer<QObject> obj(component.create());
        QVERIFY(obj != nullptr);
        QVERIFY(obj->property("rt").isValid());

        QObject *fromQml = qvariant_cast<QObject *>(obj->property("rt"));
        QVERIFY(fromQml != nullptr);
        QCOMPARE(fromQml, static_cast<QObject *>(&runtime));
    }
};

QTEST_MAIN(TestRuntimeLifetime)
#include "test_runtime_lifetime.moc"