// The models table keeps the picks the user has not saved yet.
//
// The draft used to be a plain JS object that syncDraft() rebuilt from the
// saved settings, so an unsaved edit was thrown away the moment the window
// was shown again (ADR 200). The draft is now a set of entries with real
// properties; the test checks that the entries are there, that an unsaved
// edit survives a sync, and that the Cancel path throws it away.

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QTranslator>
#include <QtTest>

#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A stand-in for the models list: no rows, so every row of the table falls
// back to "no model chosen".
class FakeQuantModel : public QAbstractListModel
{
    Q_OBJECT

public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 0; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        Q_UNUSED(index)
        Q_UNUSED(role)
        return {};
    }

    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, QByteArrayLiteral("key")}, {Qt::UserRole + 2, QByteArrayLiteral("title")}}; }

    Q_INVOKABLE QVariantMap rowFor(const QString &key) const
    {
        Q_UNUSED(key)
        QVariantMap row;
        row.insert(QStringLiteral("quants"), QVariantList());
        row.insert(QStringLiteral("selected"), -1);
        return row;
    }

    Q_INVOKABLE int rowIndexOf(const QString &key) const
    {
        Q_UNUSED(key)
        return -1;
    }

    explicit FakeQuantModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
};

// A stand-in for the ModelInstaller singleton: the table only asks it for the
// models list and watches it for changes.
class FakeModelInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *allModels READ allModels CONSTANT)
    Q_PROPERTY(QObject *quantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *checkQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *decisionQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *layoutQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *rawPageOcrQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *fixQuantModels READ quantModels CONSTANT)

public:
    QObject *allModels() const { return m_models; }
    QObject *quantModels() const { return m_models; }

signals:
    void installedChanged();
    void presetsChanged();

private:
    FakeQuantModel *m_models = new FakeQuantModel(this);
};

// A stand-in for the Runtime singleton: the gear popup's launch tab asks it
// for the memory estimate.
class FakeRuntime : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE QVariantMap estimateModelMemory(const QString &modelPath, const QString &role = QStringLiteral("pageOcr"))
    {
        Q_UNUSED(modelPath)
        Q_UNUSED(role)
        return {};
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

// An empty draft model for the gear popup's parameter tables.
class FakeDraftModel : public QAbstractListModel
{
    Q_OBJECT

public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 0; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        Q_UNUSED(index)
        Q_UNUSED(role)
        return {};
    }

    explicit FakeDraftModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
};

// A stand-in for the launch/request profile stores: the gear popup asks them
// for the parameter table and writes the values back.
class FakeProfilesStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *draftModel READ draftModel CONSTANT)

public:
    QAbstractItemModel *draftModel() const { return m_model; }

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

private:
    FakeDraftModel *m_model = new FakeDraftModel(this);
};

}  // namespace

// A translator that marks every string it is asked for with the context it
// was asked in: the QML texts must be translated under the context of the
// file that owns them, or the Russian UI falls back to English.
class ContextSpy : public QTranslator
{
public:
    QString translate(const char *context, const char *sourceText, const char * = nullptr, int = -1) const override
    {
        return QString::fromUtf8(sourceText) + QStringLiteral(" [") + QString::fromUtf8(context) + QLatin1Char(']');
    }
};

class TestTaskModelsTableQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void theTableLoadsWithoutWarnings();
    void theTaskTextsKeepTheirTranslationContext();
    void anUnsavedModelNameSurvivesTheSync();
    void theCancelPathThrowsTheEditAway();

private:
    void registerModuleTypes();
    std::unique_ptr<QObject> loadTable();
    QVariant evaluate(QObject *scope, const QString &expression);
    QObject *draftEntry(QObject *table, const QString &task);

    std::unique_ptr<FakeModelInstaller> m_modelInstaller;
    std::unique_ptr<FakeRuntime> m_runtime;
    std::unique_ptr<FakeUiController> m_uiController;
    std::unique_ptr<FakeProfilesStore> m_profiles;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<QQmlEngine> m_engine;
    QStringList m_warnings;
    QString m_qmlRoot;
};

void TestTaskModelsTableQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));

    m_modelInstaller = std::make_unique<FakeModelInstaller>();
    m_runtime = std::make_unique<FakeRuntime>();
    m_uiController = std::make_unique<FakeUiController>();
    m_settings = std::make_unique<SettingsStore>();

    qmlRegisterSingletonInstance("LLocr", 1, 0, "Settings", m_settings.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "ModelInstaller", m_modelInstaller.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", m_runtime.get());
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    // The gear popup's parameter tables ask the profile stores for their
    // draft; the stores are not part of this test, so one empty stand-in
    // answers for all of them.
    m_profiles = std::make_unique<FakeProfilesStore>();
    for (const char *name : {"LaunchProfiles", "RequestProfilesPageOcr", "RequestProfilesRawPageOcr", "RequestProfilesBlock", "RequestProfilesDecision", "RequestProfilesLayout", "RequestProfilesFix"})
        qmlRegisterSingletonInstance("LLocr", 1, 0, name, m_profiles.get());

    // The module's own singletons, read from the source tree.
    const auto registerSingleton = [this](const char *name) { qmlRegisterSingletonType(QUrl::fromLocalFile(m_qmlRoot + QStringLiteral("/%1.qml").arg(name)), "LLocr", 1, 0, name); };
    registerSingleton("BlockNames");
    registerSingleton("PageStatus");
    registerSingleton("RoleProfiles");
    registerSingleton("TaskStatus");
    registerSingleton("Theme");

    // The QML module of the app (qt_add_qml_module) turns every file into a
    // type of the LLocr module. The table is loaded from the source tree here,
    // so the same types are registered by hand — otherwise the components it
    // uses (LLOLabel, LaunchTab, …) would not resolve.
    registerModuleTypes();

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    // Common/*.qml read Theme without importing LLocr (module-wide visibility
    // inside the app); a context property covers them when the table is
    // loaded from disk.
    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Theme"), theme);
}

void TestTaskModelsTableQml::registerModuleTypes()
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

// The draft lives in QML, so the test asks QML for it.
QVariant TestTaskModelsTableQml::evaluate(QObject *scope, const QString &expression)
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

QObject *TestTaskModelsTableQml::draftEntry(QObject *table, const QString &task)
{
    const QVariant result = evaluate(table, QStringLiteral("draftEntry('%1')").arg(task));
    return result.value<QObject *>();
}

std::unique_ptr<QObject> TestTaskModelsTableQml::loadTable()
{
    m_warnings.clear();

    const QString tablePath = QDir(m_qmlRoot).filePath(QStringLiteral("Common/TaskModelsTable.qml"));
    QQmlComponent table(m_engine.get(), QUrl::fromLocalFile(tablePath));
    if (!table.isReady()) {
        m_warnings.append(table.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(table.create());
    if (!object)
        m_warnings.append(table.errorString());
    else
        object->setProperty("externalMode", true);
    return object;
}

void TestTaskModelsTableQml::theTableLoadsWithoutWarnings()
{
    const std::unique_ptr<QObject> table = loadTable();
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The rows are the tasks the C++ lists (ADR 215): the table keeps no
    // copy of the list.
    const QVariantList rows = evaluate(table.get(), QStringLiteral("taskIds")).toList();
    QStringList rowIds;
    for (const QVariant &row : rows)
        rowIds << row.toString();
    QCOMPARE(rowIds, m_settings->taskRoles());

    // Every task has an entry, and the entry carries the three values the
    // draft is made of.
    const QStringList tasks = {
        QStringLiteral("pageOcr"), QStringLiteral("rawPageOcr"), QStringLiteral("blockRecognition"), QStringLiteral("decision"), QStringLiteral("layout"), QStringLiteral("fix")};
    for (const QString &task : tasks) {
        QObject *entry = draftEntry(table.get(), task);
        QVERIFY2(entry, qPrintable(task));
        QCOMPARE(entry->property("task").toString(), task);
        QVERIFY(entry->property("key").isValid());
        QVERIFY(entry->property("quantId").isValid());
        QVERIFY(entry->property("modelName").isValid());
    }
}

void TestTaskModelsTableQml::theTaskTextsKeepTheirTranslationContext()
{
    auto *roles = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("RoleProfiles"));
    QVERIFY(roles);

    ContextSpy spy;
    QCoreApplication::installTranslator(&spy);
    m_engine->retranslate();
    const QVariant title = evaluate(roles, QStringLiteral("title('decision')"));
    const QVariant hint = evaluate(roles, QStringLiteral("samplingHint('decision')"));
    QCoreApplication::removeTranslator(&spy);
    m_engine->retranslate();

    QCOMPARE(title.toString(), QStringLiteral("Verification [RoleProfiles]"));
    QCOMPARE(hint.toString().contains(QStringLiteral("[RoleProfiles]")), true);
}

void TestTaskModelsTableQml::anUnsavedModelNameSurvivesTheSync()
{
    m_settings->setModelNameForRole(QStringLiteral("pageOcr"), QStringLiteral("saved-name"));

    const std::unique_ptr<QObject> table = loadTable();
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *entry = draftEntry(table.get(), QStringLiteral("pageOcr"));
    QVERIFY(entry);

    // The user types a name: the field writes it into the draft entry.
    entry->setProperty("modelName", QStringLiteral("typed-name"));
    entry->setProperty("nameEdited", true);

    // The window is shown again: the sync must not throw the edit away.
    evaluate(table.get(), QStringLiteral("syncDraft()"));
    QCoreApplication::processEvents();

    QCOMPARE(entry->property("modelName").toString(), QStringLiteral("typed-name"));
}

void TestTaskModelsTableQml::theCancelPathThrowsTheEditAway()
{
    m_settings->setModelNameForRole(QStringLiteral("pageOcr"), QStringLiteral("saved-name"));

    const std::unique_ptr<QObject> table = loadTable();
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QObject *entry = draftEntry(table.get(), QStringLiteral("pageOcr"));
    QVERIFY(entry);
    entry->setProperty("modelName", QStringLiteral("typed-name"));
    entry->setProperty("nameEdited", true);

    // Cancel: the edit is gone and the saved name is back.
    evaluate(table.get(), QStringLiteral("resetDraft()"));
    QCoreApplication::processEvents();

    QCOMPARE(entry->property("modelName").toString(), QStringLiteral("saved-name"));
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("llocr-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_task_models_table_qml"));
    TestTaskModelsTableQml test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_task_models_table_qml.moc"
