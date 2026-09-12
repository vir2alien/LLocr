#include <QtTest>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>

#include "app/LaunchProfileStore.h"
#include "app/SettingsStore.h"
#include "runtime/RuntimeController.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// Launch-profile catalog for tests (test binaries embed no resources): an
// empty catalog is enough — argv comes from the core fields.
QString writeEmptyLaunchCatalog(const QTemporaryDir &dir)
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("launch-presets.json"));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QByteArrayLiteral("{ \"schemaVersion\": 1, \"profiles\": [] }").constData());
    return path;
}

}  // namespace

// Verifies the §1.2 contract: QML's `Runtime` singleton must return exactly the
// instance passed to AppController (the one created in main.cpp). QML must not
// be able to create its own RuntimeController.
class TestRuntimeLifetime : public QObject {
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_runtime_lifetime"));
    }

    void qmlSingletonReturnsSameInstance()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        LaunchProfileStore launchProfiles(
            settings, writeEmptyLaunchCatalog(dir));
        RuntimeController runtime(settings, launchProfiles);

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