// The Launch settings tab: its memory-estimate sentence must not throw.
//
// The tab starts with an empty placeholder map and only fills it from
// Runtime.estimateModelMemory() once the popup opens. The label's text
// binding runs long before that — QML evaluates bindings on invisible items
// too — so an unguarded read of a missing key takes the tab down with
// "TypeError: Cannot read property 'length' of undefined" the moment the
// window is created.
//
// The test loads the shipped LaunchTab.qml and watches the engine's warnings:
// the tab must stay silent, and with an estimate in place it must show the
// sentence.

#include <QAbstractItemModel>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QtTest>

#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A stand-in for the Runtime singleton: the tab only ever asks it for the
// estimate map, and the test decides what that map holds.
class FakeRuntime : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE QVariantMap estimateModelMemory(const QString &modelPath, const QString &role = QStringLiteral("pageOcr"))
    {
        Q_UNUSED(modelPath)
        Q_UNUSED(role)
        return m_estimate;
    }

    void setEstimate(const QVariantMap &map) { m_estimate = map; }

private:
    QVariantMap m_estimate;
};

// A stand-in for LaunchProfiles: the tab reads the draft model through it and
// writes draft values back.
class FakeLaunchProfiles : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *draftModel READ draftModel CONSTANT)

public:
    QAbstractItemModel *draftModel() const { return nullptr; }

    Q_INVOKABLE void reloadDraft() {}
    Q_INVOKABLE bool setDraftValue(int row, const QString &text)
    {
        Q_UNUSED(row)
        Q_UNUSED(text)
        return true;
    }
    Q_INVOKABLE void saveDraft() {}
    Q_INVOKABLE void loadDefaultDraft() {}
    Q_INVOKABLE QString modelRuntimeNoteForRole(const QString &task) const
    {
        Q_UNUSED(task)
        return QString();
    }
};

// A stand-in for UiController: Theme reads the dark flag from it.
class FakeUiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark CONSTANT)

public:
    bool dark() const { return false; }
};

// The estimate map of a model the catalog knows: every label present, the
// vision cost included.
QVariantMap fullEstimate()
{
    QVariantMap map;
    map.insert(QStringLiteral("valid"), true);
    map.insert(QStringLiteral("visionLabel"), QStringLiteral("30.0 MiB"));
    map.insert(QStringLiteral("requiredLabel"), QStringLiteral("1.0 GiB"));
    map.insert(QStringLiteral("vramLabel"), QStringLiteral("8.0 GiB"));
    map.insert(QStringLiteral("modelLabel"), QStringLiteral("500.0 MiB"));
    map.insert(QStringLiteral("mmprojLabel"), QStringLiteral("10.0 MiB"));
    map.insert(QStringLiteral("draftLabel"), QString());
    map.insert(QStringLiteral("kvLabel"), QStringLiteral("200.0 MiB"));
    map.insert(QStringLiteral("computeLabel"), QStringLiteral("100.0 MiB"));
    map.insert(QStringLiteral("overheadLabel"), QStringLiteral("50.0 MiB"));
    map.insert(QStringLiteral("overVramBudget"), false);
    return map;
}

}  // namespace

class TestLaunchTabQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void estimateLabelStaysSilentWithoutAnEstimate();
    void estimateLabelStaysSilentWithAnEstimate();
    void estimateSentenceCarriesTheVisionTerm();
    void estimateSentenceWithoutTheVisionTerm();

private:
    std::unique_ptr<QObject> loadTab();
    QString estimateSentence(const QObject *tab) const;

    std::unique_ptr<FakeRuntime> m_runtime;
    std::unique_ptr<FakeLaunchProfiles> m_launchProfiles;
    std::unique_ptr<FakeUiController> m_uiController;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<QQmlEngine> m_engine;
    QStringList m_warnings;
    QString m_qmlRoot;
};

void TestLaunchTabQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));

    m_runtime = std::make_unique<FakeRuntime>();
    m_launchProfiles = std::make_unique<FakeLaunchProfiles>();
    m_uiController = std::make_unique<FakeUiController>();
    m_settings = std::make_unique<SettingsStore>();

    // The tab reaches for these through `import LLocr`. The module is
    // registered by hand here (the app does the same in main.cpp), because
    // the QML module itself is compiled into the executable, not into a
    // library a test could link.
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", m_settings.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", m_runtime.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "LaunchProfiles", m_launchProfiles.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    // The module's own singletons, read from the source tree.
    const auto registerSingleton = [this](const char *name) { qmlRegisterSingletonType(QUrl::fromLocalFile(m_qmlRoot + QStringLiteral("/%1.qml").arg(name)), "LLocr", 1, 0, name); };
    registerSingleton("BlockNames");
    registerSingleton("PageStatus");
    registerSingleton("RoleProfiles");
    registerSingleton("TaskStatus");
    registerSingleton("Theme");

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    // Common/*.qml read Theme without importing LLocr (module-wide visibility
    // inside the app); a context property covers them when the tab is loaded
    // from disk.
    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Theme"), theme);
}

std::unique_ptr<QObject> TestLaunchTabQml::loadTab()
{
    m_warnings.clear();

    const QString tabPath = QDir(m_qmlRoot).filePath(QStringLiteral("Settings/LaunchTab.qml"));
    QQmlComponent tab(m_engine.get(), QUrl::fromLocalFile(tabPath));
    if (!tab.isReady()) {
        m_warnings.append(tab.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(tab.create());
    if (!object)
        m_warnings.append(tab.errorString());
    return object;
}

QString TestLaunchTabQml::estimateSentence(const QObject *tab) const
{
    for (QObject *child : tab->findChildren<QObject *>()) {
        const QString text = child->property("text").toString();
        if (text.contains(QStringLiteral("Video memory for this model")))
            return text;
    }
    return QString();
}

void TestLaunchTabQml::estimateLabelStaysSilentWithoutAnEstimate()
{
    m_settings->setConnectionMode(QStringLiteral("managed"));

    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY(tab);
    QCoreApplication::processEvents();

    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));
}

void TestLaunchTabQml::estimateLabelStaysSilentWithAnEstimate()
{
    m_settings->setConnectionMode(QStringLiteral("managed"));
    m_runtime->setEstimate(fullEstimate());

    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY(tab);
    QVERIFY(QMetaObject::invokeMethod(tab.get(), "loadValues"));
    QCoreApplication::processEvents();

    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));
}

void TestLaunchTabQml::estimateSentenceCarriesTheVisionTerm()
{
    m_settings->setConnectionMode(QStringLiteral("managed"));
    m_runtime->setEstimate(fullEstimate());

    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY(tab);
    QVERIFY(QMetaObject::invokeMethod(tab.get(), "loadValues"));
    QCoreApplication::processEvents();

    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    const QString sentence = estimateSentence(tab.get());
    QVERIFY2(!sentence.isEmpty(), "the tab must show the video-memory sentence");
    QVERIFY2(sentence.contains(QStringLiteral("vision")), qPrintable(sentence));
    QVERIFY2(sentence.contains(QStringLiteral("30.0 MiB")), qPrintable(sentence));
}

void TestLaunchTabQml::estimateSentenceWithoutTheVisionTerm()
{
    m_settings->setConnectionMode(QStringLiteral("managed"));
    QVariantMap estimate = fullEstimate();
    estimate.insert(QStringLiteral("visionLabel"), QString());
    m_runtime->setEstimate(estimate);

    const std::unique_ptr<QObject> tab = loadTab();
    QVERIFY(tab);
    QVERIFY(QMetaObject::invokeMethod(tab.get(), "loadValues"));
    QCoreApplication::processEvents();

    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    const QString sentence = estimateSentence(tab.get());
    QVERIFY2(!sentence.isEmpty(), "the tab must show the video-memory sentence");
    QVERIFY2(!sentence.contains(QStringLiteral("vision")), qPrintable(sentence));
}

QTEST_MAIN(TestLaunchTabQml)
#include "test_launch_tab_qml.moc"
