// The connection tab of a task: the alias, the endpoint and the key.
//
// The tab is a draft: a keystroke changes the draft and marks the window
// dirty, and the settings are written when the window is saved. The key goes
// to the OS secret store, and the stored key is never shown back — the tab
// shows that a key is there, not what it is.

#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>

#include "config/SettingsStore.h"
#include "core/ConnectionRole.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// The keystrokes of the test: every character goes through the window as a
// real key event, so the field's onTextEdited fires the way it does for the
// user.
void typeText(QQuickWindow *window, const QString &text)
{
    for (const QChar &character : text)
        QTest::keyClick(window, character.toLatin1(), Qt::NoModifier, 0);
}

// A stand-in for the UiController singleton: Theme reads the dark flag and
// the language from it.
class FakeUiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark CONSTANT)
    Q_PROPERTY(QString language READ language CONSTANT)

public:
    bool dark() const { return false; }
    QString language() const { return QStringLiteral("en"); }
};

}  // namespace

class TestTaskConnectionTabQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void theTabLoadsWithoutWarnings();
    void theAliasIsWrittenOnSaveOnly();
    void theCancelPathThrowsTheDraftsAway();
    void theEndpointGoesThroughTheSettings();
    void theKeyGoesToTheSecretStore();
    void theStoredKeyIsNeverShownBack();
    void theTaskEndpointWarnsAboutPlaintextKey();

private:
    void registerModuleTypes();
    std::unique_ptr<QObject> loadTab(const QString &task);
    QVariant evaluate(QObject *scope, const QString &expression);
    QList<QQuickItem *> itemsByType(QQuickItem *root, const QString &typeName);
    QQuickItem *textField(QObject *tab, const QString &placeholder);
    QQuickItem *keyField(QObject *tab);
    QQuickItem *labelWithText(QObject *tab, const QString &text);

    std::unique_ptr<FakeUiController> m_uiController;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QStringList m_warnings;
    QString m_qmlRoot;
    QString m_task;
};

void TestTaskConnectionTabQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    m_task = QStringLiteral("decision");

    m_uiController = std::make_unique<FakeUiController>();
    m_settings = std::make_unique<SettingsStore>();

    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", m_settings.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    // The module's own singletons, read from the source tree.
    const auto registerSingleton = [this](const char *name) { qmlRegisterSingletonType(QUrl::fromLocalFile(m_qmlRoot + QStringLiteral("/%1.qml").arg(name)), "LLocr", 1, 0, name); };
    registerSingleton("Theme");

    registerModuleTypes();

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Theme"), theme);

    m_window = std::make_unique<QQuickWindow>();
    m_window->resize(720, 600);
    m_window->show();
}

void TestTaskConnectionTabQml::init()
{
    m_settings->setModelNameForRole(m_task, QString());
    m_settings->setBaseUrlForRole(m_task, QString());
    m_settings->clearApiKeyForRole(m_task);
}

void TestTaskConnectionTabQml::cleanup()
{
    m_settings->clearApiKeyForRole(m_task);
    m_settingsIsolation.clearSecrets();
}

void TestTaskConnectionTabQml::registerModuleTypes()
{
    const QDir root(m_qmlRoot);
    const auto registerFile = [](const QFileInfo &info) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QByteArray head = file.read(64);
        const bool singleton = head.contains("pragma Singleton");
        const QUrl url = QUrl::fromLocalFile(info.absoluteFilePath());
        if (singleton)
            qmlRegisterSingletonType(url, "LLocr", 1, 0, info.completeBaseName().toLatin1().constData());
        else
            qmlRegisterType(url, "LLocr", 1, 0, info.completeBaseName().toLatin1().constData());
    };

    const QFileInfoList entries = root.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (info.isDir()) {
            const QDir sub(info.absoluteFilePath());
            const QFileInfoList files = sub.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &file : files) {
                if (file.suffix() == QLatin1String("qml"))
                    registerFile(file);
            }
            continue;
        }
        if (info.suffix() == QLatin1String("qml"))
            registerFile(info);
    }
}

QVariant TestTaskConnectionTabQml::evaluate(QObject *scope, const QString &expression)
{
    QQmlContext *context = m_engine->contextForObject(scope);
    if (!context)
        return {};
    QQmlExpression expr(context, scope, expression);
    if (expr.hasError()) {
        m_warnings.append(expr.error().toString());
        return {};
    }
    return expr.evaluate();
}

// The tab in a window of its own: the fields need a focus item, and the
// keystrokes of the test must reach them.
std::unique_ptr<QObject> TestTaskConnectionTabQml::loadTab(const QString &task)
{
    m_warnings.clear();

    const QString tabPath = QDir(m_qmlRoot).filePath(QStringLiteral("Settings/TaskConnectionTab.qml"));
    QQmlComponent tab(m_engine.get(), QUrl::fromLocalFile(tabPath));
    if (!tab.isReady()) {
        m_warnings.append(tab.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(tab.create());
    if (!object) {
        m_warnings.append(tab.errorString());
        return nullptr;
    }
    if (auto *item = qobject_cast<QQuickItem *>(object.get())) {
        item->setParentItem(m_window->contentItem());
        item->setWidth(640);
    }
    object->setProperty("task", task);
    QCoreApplication::processEvents();
    return object;
}

// Every item of the given QML type in the visual tree of the root.
QList<QQuickItem *> TestTaskConnectionTabQml::itemsByType(QQuickItem *root, const QString &typeName)
{
    QList<QQuickItem *> found;
    if (!root)
        return found;
    // A QML type's class name carries the engine's suffix
    // ("TextField_QMLTYPE_23"), so the name is matched by its prefix.
    const QString name = QString::fromLatin1(root->metaObject()->className());
    if (name == typeName || name.startsWith(typeName + QStringLiteral("_QMLTYPE")))
        found.append(root);
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children)
        found.append(itemsByType(child, typeName));
    return found;
}

// The text field with the given placeholder: the fields of the tab tell each
// other apart by what they stand for.
QQuickItem *TestTaskConnectionTabQml::textField(QObject *tab, const QString &placeholder)
{
    auto *root = qobject_cast<QQuickItem *>(tab);
    if (!root)
        return nullptr;
    const QList<QQuickItem *> fields = itemsByType(root, QStringLiteral("TextField"));
    for (QQuickItem *field : fields) {
        if (field->property("placeholderText").toString() == placeholder)
            return field;
    }
    return nullptr;
}

// The field of the key: it is the only one that hides its text.
QQuickItem *TestTaskConnectionTabQml::keyField(QObject *tab)
{
    auto *root = qobject_cast<QQuickItem *>(tab);
    if (!root)
        return nullptr;
    const QList<QQuickItem *> fields = itemsByType(root, QStringLiteral("TextField"));
    for (QQuickItem *field : fields) {
        if (field->property("echoMode").toInt() == 2)  // TextInput.Password
            return field;
    }
    return nullptr;
}

// The label with the given text: the fields of the tab tell each other
// apart by what they stand for, and so do the labels.
QQuickItem *TestTaskConnectionTabQml::labelWithText(QObject *tab, const QString &text)
{
    auto *root = qobject_cast<QQuickItem *>(tab);
    if (!root)
        return nullptr;
    const QList<QQuickItem *> labels = itemsByType(root, QStringLiteral("LLOLabel"));
    for (QQuickItem *label : labels) {
        if (label->property("text").toString() == text)
            return label;
    }
    return nullptr;
}

void TestTaskConnectionTabQml::theTabLoadsWithoutWarnings()
{
    const std::unique_ptr<QObject> tab = loadTab(m_task);
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The drafts come from the settings of the task.
    QCOMPARE(evaluate(tab.get(), QStringLiteral("aliasDraft")).toString(), m_settings->modelNameForRole(m_task));
    QCOMPARE(evaluate(tab.get(), QStringLiteral("endpointDraft")).toString(), m_settings->baseUrlForRole(m_task));
    QCOMPARE(evaluate(tab.get(), QStringLiteral("keyPresent")).toBool(), m_settings->apiKeySetForRole(m_task));
    QCOMPARE(evaluate(tab.get(), QStringLiteral("keyDraft")).toString(), QString());
}

// The alias is a draft: the keystroke changes the draft, and the settings
// take it when the window is saved.
void TestTaskConnectionTabQml::theAliasIsWrittenOnSaveOnly()
{
    m_settings->setModelNameForRole(m_task, QStringLiteral("saved-alias"));

    const std::unique_ptr<QObject> tab = loadTab(m_task);
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QQuickItem *alias = textField(tab.get(), QStringLiteral("the model id"));
    QVERIFY(alias);
    alias->setProperty("text", QString());
    alias->forceActiveFocus();
    typeText(m_window.get(), QStringLiteral("typed-alias"));
    QCoreApplication::processEvents();

    // The draft took the keystroke; the settings did not.
    QCOMPARE(evaluate(tab.get(), QStringLiteral("aliasDraft")).toString(), QStringLiteral("typed-alias"));
    QCOMPARE(m_settings->modelNameForRole(m_task), QStringLiteral("saved-alias"));

    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(tab.get(), "saveValues", Q_RETURN_ARG(QVariant, result)));
    QCOMPARE(m_settings->modelNameForRole(m_task), QStringLiteral("typed-alias"));
}

// Cancel throws the drafts of the alias, the endpoint and the key away.
void TestTaskConnectionTabQml::theCancelPathThrowsTheDraftsAway()
{
    m_settings->setModelNameForRole(m_task, QStringLiteral("saved-alias"));
    m_settings->setBaseUrlForRole(m_task, QStringLiteral("http://saved.example"));

    const std::unique_ptr<QObject> tab = loadTab(m_task);
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QQuickItem *alias = textField(tab.get(), QStringLiteral("the model id"));
    QVERIFY(alias);
    alias->setProperty("text", QString());
    alias->forceActiveFocus();
    typeText(m_window.get(), QStringLiteral("typed-alias"));
    QCoreApplication::processEvents();

    QQuickItem *endpoint = textField(tab.get(), m_settings->baseUrl());
    QVERIFY(endpoint);
    endpoint->setProperty("text", QString());
    endpoint->forceActiveFocus();
    typeText(m_window.get(), QStringLiteral("http://typed.example"));
    QCoreApplication::processEvents();

    QCOMPARE(evaluate(tab.get(), QStringLiteral("aliasDraft")).toString(), QStringLiteral("typed-alias"));
    QCOMPARE(evaluate(tab.get(), QStringLiteral("endpointDraft")).toString(), QStringLiteral("http://typed.example"));

    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(tab.get(), "resetValues", Q_RETURN_ARG(QVariant, result)));
    QCoreApplication::processEvents();

    // The drafts are the saved ones again, and the settings never moved.
    QCOMPARE(evaluate(tab.get(), QStringLiteral("aliasDraft")).toString(), QStringLiteral("saved-alias"));
    QCOMPARE(evaluate(tab.get(), QStringLiteral("endpointDraft")).toString(), QStringLiteral("http://saved.example"));
    QCOMPARE(evaluate(tab.get(), QStringLiteral("keyDraft")).toString(), QString());
    QCOMPARE(alias->property("text").toString(), QStringLiteral("saved-alias"));
    QCOMPARE(endpoint->property("text").toString(), QStringLiteral("http://saved.example"));
    QCOMPARE(m_settings->modelNameForRole(m_task), QStringLiteral("saved-alias"));
    QCOMPARE(m_settings->baseUrlForRole(m_task), QStringLiteral("http://saved.example"));
}

// The endpoint of the task goes through the settings of the task.
void TestTaskConnectionTabQml::theEndpointGoesThroughTheSettings()
{
    const std::unique_ptr<QObject> tab = loadTab(m_task);
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QQuickItem *endpoint = textField(tab.get(), m_settings->baseUrl());
    QVERIFY(endpoint);
    endpoint->setProperty("text", QString());
    endpoint->forceActiveFocus();
    typeText(m_window.get(), QStringLiteral("http://typed.example"));
    QCoreApplication::processEvents();

    QCOMPARE(evaluate(tab.get(), QStringLiteral("endpointDraft")).toString(), QStringLiteral("http://typed.example"));
    QCOMPARE(m_settings->baseUrlForRole(m_task), QString());

    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(tab.get(), "saveValues", Q_RETURN_ARG(QVariant, result)));
    QCOMPARE(m_settings->baseUrlForRole(m_task), QStringLiteral("http://typed.example"));
}

// The key of the task goes to the OS secret store, and only on save.
void TestTaskConnectionTabQml::theKeyGoesToTheSecretStore()
{
    const std::unique_ptr<QObject> tab = loadTab(m_task);
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QQuickItem *key = keyField(tab.get());
    QVERIFY(key);
    key->setProperty("text", QString());
    key->forceActiveFocus();
    typeText(m_window.get(), QStringLiteral("typed-key"));
    QCoreApplication::processEvents();

    QCOMPARE(evaluate(tab.get(), QStringLiteral("keyDraft")).toString(), QStringLiteral("typed-key"));
    QCOMPARE(evaluate(tab.get(), QStringLiteral("keyEdited")).toBool(), true);
    // The keystroke alone writes nothing.
    QCOMPARE(m_settings->apiKeySetForRole(m_task), false);

    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(tab.get(), "saveValues", Q_RETURN_ARG(QVariant, result)));

    QCOMPARE(m_settings->apiKeySetForRole(m_task), true);
    QCOMPARE(m_settings->apiKeyForRole(m_task), QStringLiteral("typed-key"));
}

// A stored key is never shown back: the field carries the mask of a key, and
// the tab knows that a key is there.
void TestTaskConnectionTabQml::theStoredKeyIsNeverShownBack()
{
    m_settings->setApiKeyForRole(m_task, QStringLiteral("stored-secret"));

    const std::unique_ptr<QObject> tab = loadTab(m_task);
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QCOMPARE(evaluate(tab.get(), QStringLiteral("keyPresent")).toBool(), true);
    QCOMPARE(evaluate(tab.get(), QStringLiteral("keyDraft")).toString(), QString());

    QQuickItem *key = keyField(tab.get());
    QVERIFY(key);
    const QString shown = key->property("text").toString();
    QVERIFY(!shown.isEmpty());
    QVERIFY(shown != QStringLiteral("stored-secret"));

    // The mask of the key field is what the field shows.
    auto *root = qobject_cast<QQuickItem *>(tab.get());
    QVERIFY(root);
    const QList<QQuickItem *> cells = itemsByType(root, QStringLiteral("SecretKeyField"));
    QCOMPARE(cells.size(), 1);
    QCOMPARE(shown, cells.first()->property("maskText").toString());
}

// The key of a task goes out in plain text over an http endpoint outside
// the loopback: the warning must follow what the user types.
void TestTaskConnectionTabQml::theTaskEndpointWarnsAboutPlaintextKey()
{
    const std::unique_ptr<QObject> tab = loadTab(m_task);
    QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QQuickItem *endpoint = textField(tab.get(), m_settings->baseUrl());
    QVERIFY(endpoint);
    QQuickItem *warning = labelWithText(tab.get(), QStringLiteral("The key will be sent in plain text (http outside the local machine)."));
    QVERIFY(warning);
    QCOMPARE(warning->property("visible").toBool(), false);

    endpoint->setProperty("text", QString());
    endpoint->forceActiveFocus();
    typeText(m_window.get(), QStringLiteral("http://192.168.1.5:8080"));
    QCoreApplication::processEvents();
    QCOMPARE(warning->property("visible").toBool(), true);

    // A local endpoint is quiet again.
    endpoint->setProperty("text", QString());
    endpoint->forceActiveFocus();
    typeText(m_window.get(), QStringLiteral("http://localhost:8080"));
    QCoreApplication::processEvents();
    QCOMPARE(warning->property("visible").toBool(), false);
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_task_connection_tab_qml"));
    TestTaskConnectionTabQml test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_task_connection_tab_qml.moc"
