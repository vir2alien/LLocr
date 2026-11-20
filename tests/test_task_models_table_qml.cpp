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
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTranslator>
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

    // The quantization rows of a model: the ids, the sizes and the memory
    // floors — the numbers behind a quantization, the reason the dropdown is
    // opened at all.
    QVariantList quants() const
    {
        QVariantList list;
        list.append(quant(QStringLiteral("Q4_K_XL"), QStringLiteral("13.3 GiB"), QStringLiteral("13.3 GiB"), QStringLiteral("16.0 GiB"), QStringLiteral("3.9 GiB"), true));
        list.append(quant(QStringLiteral("Q8_0"), QStringLiteral("18.6 GiB"), QStringLiteral("18.6 GiB"), QString(), QString(), false));
        return list;
    }

    Q_INVOKABLE QVariantMap rowFor(const QString &key) const
    {
        Q_UNUSED(key)
        QVariantMap row;
        row.insert(QStringLiteral("quants"), quants());
        row.insert(QStringLiteral("selected"), -1);
        return row;
    }

    Q_INVOKABLE int rowIndexOf(const QString &key) const
    {
        Q_UNUSED(key)
        return -1;
    }

    explicit FakeQuantModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

private:
    static QVariantMap quant(const QString &id, const QString &size, const QString &vram, const QString &ram, const QString &vramFloor, bool cpuOffload)
    {
        QVariantMap item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("sizeLabel"), size);
        item.insert(QStringLiteral("vramLabel"), vram);
        item.insert(QStringLiteral("ramLabel"), ram);
        item.insert(QStringLiteral("vramFloorLabel"), vramFloor);
        item.insert(QStringLiteral("cpuOffload"), cpuOffload);
        item.insert(QStringLiteral("installed"), false);
        item.insert(QStringLiteral("active"), false);
        item.insert(QStringLiteral("downloadable"), true);
        return item;
    }
};

// A stand-in for the ModelInstaller singleton: the table only asks it for the
// models list and watches it for changes.
class FakeModelInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *allModels READ allModels CONSTANT)
    Q_PROPERTY(QObject *checkQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *decisionQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *layoutQuantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *fixQuantModels READ quantModels CONSTANT)

public:
    QObject *allModels() const { return m_models; }
    QObject *quantModels() const { return m_models; }
    QVariantList quants() const { return m_models->quants(); }

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
    Q_INVOKABLE QVariantMap estimateModelMemory(const QString &modelPath, const QString &role = QStringLiteral("blockRecognition"))
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
    Q_INVOKABLE void saveDraft() { ++m_saves; }
    Q_INVOKABLE void loadDefaultDraft() {}
    Q_INVOKABLE bool draftHasSecretLikeParameter() const { return m_secretLike; }
    Q_INVOKABLE QString modelRuntimeNoteForRole(const QString &task) const
    {
        Q_UNUSED(task)
        return QString();
    }

    void setSecretLike(bool value) { m_secretLike = value; }
    void resetSaves() { m_saves = 0; }
    int saves() const { return m_saves; }

private:
    FakeDraftModel *m_model = new FakeDraftModel(this);
    bool m_secretLike = false;
    int m_saves = 0;
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
    void theTableShowsARuntimeColumnPerTask();
    void theRuntimeColumnSwitchesTheTask();
    void theSplitFollowsTheDraft();
    void anUnsavedModelNameSurvivesTheSync();
    void theCancelPathThrowsTheEditAway();
    void theSaveTabsWritesEveryTab();
    void theSaveTabsAsksBeforeWritingTheLaunchDraft();
    void theQuantizationFieldFitsTheQuantization();
    void theQuantizationPopupIsAsWideAsItsWidestEntry();
    void theColumnsFitTheNarrowWindowOfTheWizard();
    void theColumnsFitTheSettingsWindow();
    void theGearStandsNextToTheFields();

private:
    void registerModuleTypes();
    void useManagedTasks();
    std::unique_ptr<QObject> loadTable();
    std::unique_ptr<QObject> loadTable(QQuickItem *host);
    std::unique_ptr<QObject> loadTable(QQuickItem *host, bool showRuntimeColumn);
    QVariant evaluate(QObject *scope, const QString &expression);
    QObject *draftEntry(QObject *table, const QString &task);
    QQuickItem *comboBox(QObject *table, const QString &textRole);
    QQuickItem *textField(QObject *scope, const QString &placeholder);
    QObject *taskSettingsPopup(QObject *table);
    QList<QQuickItem *> itemsByType(QQuickItem *root, const QString &typeName);

    std::unique_ptr<FakeModelInstaller> m_modelInstaller;
    std::unique_ptr<FakeRuntime> m_runtime;
    std::unique_ptr<FakeUiController> m_uiController;
    std::unique_ptr<FakeProfilesStore> m_profiles;
    std::unique_ptr<FakeProfilesStore> m_launchProfiles;
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
    m_launchProfiles = std::make_unique<FakeProfilesStore>();
    qmlRegisterSingletonInstance("LLocr", 1, 0, "LaunchProfiles", m_launchProfiles.get());
    for (const char *name : {"RequestProfilesBlock", "RequestProfilesDecision", "RequestProfilesLayout", "RequestProfilesFix"})
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

    // The quantization rows of the fake model, so the test can measure the
    // texts of the dropdown without reaching into the model's own rows.
    m_engine->rootContext()->setContextProperty(QStringLiteral("FakeQuants"), m_modelInstaller->quants());
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
    return loadTable(nullptr);
}

// The table in a host of its own: the host gives the table the width of the
// window it stands in, so the test measures the row as it is drawn.
std::unique_ptr<QObject> TestTaskModelsTableQml::loadTable(QQuickItem *host)
{
    return loadTable(host, true);
}

// The table as the host shows it: the setup wizard hides the runtime column,
// the settings window shows it.
std::unique_ptr<QObject> TestTaskModelsTableQml::loadTable(QQuickItem *host, bool showRuntimeColumn)
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
    else {
        object->setProperty("showRuntimeColumn", showRuntimeColumn);
        if (host) {
            if (auto *item = qobject_cast<QQuickItem *>(object.get())) {
                item->setParentItem(host);
                item->setWidth(host->width());
                item->setHeight(host->height());
            }
        }
        // The draft takes the runtimes of the host it stands in.
        evaluate(object.get(), QStringLiteral("syncDraft()"));
    }
    return object;
}

// Every item of the given QML type in the visual tree of the root.
QList<QQuickItem *> TestTaskModelsTableQml::itemsByType(QQuickItem *root, const QString &typeName)
{
    QList<QQuickItem *> found;
    if (!root)
        return found;
    // A QML type's class name carries the engine's suffix
    // ("LLOComboBox_QMLTYPE_23"), so the name is matched by its prefix.
    const QString name = QString::fromLatin1(root->metaObject()->className());
    if (name == typeName || name.startsWith(typeName + QStringLiteral("_QMLTYPE")))
        found.append(root);
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children)
        found.append(itemsByType(child, typeName));
    return found;
}

// The dropdown of the table that reads the given role: the name of a model
// or the quantization of a task.
QQuickItem *TestTaskModelsTableQml::comboBox(QObject *table, const QString &textRole)
{
    auto *root = qobject_cast<QQuickItem *>(table);
    if (!root)
        return nullptr;
    const QList<QQuickItem *> combos = itemsByType(root, QStringLiteral("LLOComboBox"));
    for (QQuickItem *combo : combos) {
        if (combo->property("textRole").toString() == textRole)
            return combo;
    }
    return nullptr;
}

// The text field with the given placeholder: the fields of the table and of
// the gear popup tell each other apart by what they stand for.
QQuickItem *TestTaskModelsTableQml::textField(QObject *scope, const QString &placeholder)
{
    auto *root = qobject_cast<QQuickItem *>(scope);
    if (!root)
        return nullptr;
    const QList<QQuickItem *> fields = itemsByType(root, QStringLiteral("TextField"));
    for (QQuickItem *field : fields) {
        if (field->property("placeholderText").toString() == placeholder)
            return field;
    }
    return nullptr;
}

// The popup of the gear: the one popup of the table that knows a task.
QObject *TestTaskModelsTableQml::taskSettingsPopup(QObject *table)
{
    const QList<QObject *> children = table->findChildren<QObject *>();
    for (QObject *child : children) {
        const QString name = QString::fromLatin1(child->metaObject()->className());
        if (!name.startsWith(QStringLiteral("Popup")) && !name.startsWith(QStringLiteral("QQuickPopup")))
            continue;
        // The popup of the gear is the one that knows a task.
        if (child->property("task").isValid())
            return child;
    }
    return nullptr;
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
    const QStringList tasks = {QStringLiteral("blockRecognition"), QStringLiteral("decision"), QStringLiteral("layout"), QStringLiteral("fix")};
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

// Every task has a row of its own and a runtime of its own: the table asks
// the settings for the mode of each task.
void TestTaskModelsTableQml::theTableShowsARuntimeColumnPerTask()
{
    const std::unique_ptr<QObject> table = loadTable();
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCOMPARE(evaluate(table.get(), QStringLiteral("draft.length")).toInt(), 4);
    for (const QString &role : connectionRoleIds()) {
        const QString mode = evaluate(table.get(), QStringLiteral("runtimeModeForTask('%1')").arg(role)).toString();
        QVERIFY2(!mode.isEmpty(), qPrintable(QStringLiteral("no runtime mode for %1").arg(role)));
    }
}

// The switch of the row writes the mode of that task into the draft: the
// settings take it when the window is saved, so the user can still cancel it
// away.
void TestTaskModelsTableQml::theRuntimeColumnSwitchesTheTask()
{
    m_settings->setRuntimeModeForRole(QStringLiteral("decision"), QStringLiteral("external"));

    const std::unique_ptr<QObject> table = loadTable();
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QSignalSpy editedSpy(table.get(), SIGNAL(edited()));
    evaluate(table.get(), QStringLiteral("setRuntimeModeForTask('decision','managed')"));
    QCoreApplication::processEvents();

    QObject *entry = draftEntry(table.get(), QStringLiteral("decision"));
    QVERIFY(entry);
    QCOMPARE(entry->property("runtimeMode").toString(), QStringLiteral("managed"));
    QCOMPARE(entry->property("runtimeModeEdited").toBool(), true);
    QCOMPARE(editedSpy.count(), 1);

    // The settings keep the saved mode until the window is saved.
    QCOMPARE(m_settings->runtimeModeForRole(QStringLiteral("decision")), QStringLiteral("external"));
}

// The split of the tasks follows the draft: the counter of the host and the
// rows on screen must agree.
void TestTaskModelsTableQml::theSplitFollowsTheDraft()
{
    for (const QString &role : connectionRoleIds())
        m_settings->setRuntimeModeForRole(role, QStringLiteral("managed"));

    const std::unique_ptr<QObject> table = loadTable();
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QCOMPARE(evaluate(table.get(), QStringLiteral("managedCount")).toInt(), 4);
    QCOMPARE(evaluate(table.get(), QStringLiteral("externalCount")).toInt(), 0);
    QCOMPARE(evaluate(table.get(), QStringLiteral("externalTaskSummary")).toString(), QString());

    evaluate(table.get(), QStringLiteral("setRuntimeModeForTask('decision','external')"));
    QCoreApplication::processEvents();

    QCOMPARE(evaluate(table.get(), QStringLiteral("managedCount")).toInt(), 3);
    QCOMPARE(evaluate(table.get(), QStringLiteral("externalCount")).toInt(), 1);
    QCOMPARE(evaluate(table.get(), QStringLiteral("externalTaskSummary")).toString(), QStringLiteral("Verification"));
}

void TestTaskModelsTableQml::anUnsavedModelNameSurvivesTheSync()
{
    m_settings->setModelNameForRole(QStringLiteral("blockRecognition"), QStringLiteral("saved-name"));

    const std::unique_ptr<QObject> table = loadTable();
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *entry = draftEntry(table.get(), QStringLiteral("blockRecognition"));
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
    m_settings->setModelNameForRole(QStringLiteral("blockRecognition"), QStringLiteral("saved-name"));

    const std::unique_ptr<QObject> table = loadTable();
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QObject *entry = draftEntry(table.get(), QStringLiteral("blockRecognition"));
    QVERIFY(entry);
    entry->setProperty("modelName", QStringLiteral("typed-name"));
    entry->setProperty("nameEdited", true);

    // Cancel: the edit is gone and the saved name is back.
    evaluate(table.get(), QStringLiteral("resetDraft()"));
    QCoreApplication::processEvents();

    QCOMPARE(entry->property("modelName").toString(), QStringLiteral("saved-name"));
}

// The tabs of the gear popup are written together: the launch tab answers
// whether it wrote anything, the request tab is written either way, and the
// connection tab writes the alias and the endpoint of its task.
void TestTaskModelsTableQml::theSaveTabsWritesEveryTab()
{
    m_launchProfiles->resetSaves();
    m_profiles->resetSaves();

    // An external task shows the connection tab of the gear popup.
    m_settings->setRuntimeModeForRole(QStringLiteral("decision"), QStringLiteral("external"));

    QQuickWindow window;
    window.resize(1040, 600);
    window.show();

    QQuickItem *host = new QQuickItem(window.contentItem());
    host->setSize(QSizeF(1040, 600));

    const std::unique_ptr<QObject> table = loadTable(host);
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    // The gear popup of the decision task: its connection tab holds the
    // alias and the endpoint of that task.
    evaluate(table.get(), QStringLiteral("openTaskSettings('decision')"));
    QCoreApplication::processEvents();
    QTest::qWait(20);

    QObject *popup = taskSettingsPopup(table.get());
    QVERIFY(popup);
    QQuickItem *content = qobject_cast<QQuickItem *>(popup->property("contentItem").value<QObject *>());
    QVERIFY(content);
    QQuickItem *alias = textField(content, QStringLiteral("the model id"));
    QVERIFY(alias);
    alias->setProperty("text", QString());
    alias->forceActiveFocus();
    typeText(&window, QStringLiteral("typed-alias"));
    QCoreApplication::processEvents();

    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(table.get(), "saveTabs", Q_RETURN_ARG(QVariant, result)));
    QCOMPARE(result.toBool(), true);
    QCOMPARE(m_launchProfiles->saves(), 1);
    QCOMPARE(m_profiles->saves(), 1);

    // The connection tab wrote the alias of its task.
    QCOMPARE(m_settings->modelNameForRole(QStringLiteral("decision")), QStringLiteral("typed-alias"));
}

void TestTaskModelsTableQml::theSaveTabsAsksBeforeWritingTheLaunchDraft()
{
    m_launchProfiles->setSecretLike(true);
    m_launchProfiles->resetSaves();
    m_profiles->resetSaves();

    const std::unique_ptr<QObject> table = loadTable();
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCoreApplication::processEvents();

    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(table.get(), "saveTabs", Q_RETURN_ARG(QVariant, result)));
    QCOMPARE(result.toBool(), false);
    // The launch tab asked the user a question and wrote nothing; the
    // request tab was written anyway.
    QCOMPARE(m_launchProfiles->saves(), 0);
    QCOMPARE(m_profiles->saves(), 1);
}

// The columns of the table have their own width: the name of a quantization
// and its marker must fit into the field, and the name of a model must not
// take the room of the rest of the row (ADR 321).
void TestTaskModelsTableQml::theQuantizationFieldFitsTheQuantization()
{
    useManagedTasks();

    QQuickWindow window;
    window.resize(1040, 600);
    window.show();

    QQuickItem *host = new QQuickItem(window.contentItem());
    host->setSize(QSizeF(1040, 600));

    const std::unique_ptr<QObject> table = loadTable(host);
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    // The layout is applied in the polish: let it happen before the test
    // measures the row.
    QTest::qWait(20);

    QQuickItem *quant = comboBox(table.get(), QStringLiteral("id"));
    QVERIFY(quant);
    QQuickItem *model = comboBox(table.get(), QStringLiteral("title"));
    QVERIFY(model);

    // The field fits the id of a quantization and its marker, with the
    // padding of the control around them.
    const double text = evaluate(table.get(), QStringLiteral("textWidth('Q4_K_XL  ⬇')")).toDouble();
    QVERIFY(text > 0);
    const double padding = quant->property("leftPadding").toDouble() + quant->property("rightPadding").toDouble();
    QVERIFY(quant->property("width").toDouble() >= text + padding);

    // The columns keep the width they were given: the free space of the row
    // goes to the gap before the gear, not to the name of the model.
    QCOMPARE(model->property("width").toDouble(), static_cast<double>(table->property("modelColumnWidth").toInt()));
    QCOMPARE(quant->property("width").toDouble(), static_cast<double>(table->property("quantColumnWidth").toInt()));
}

// The dropdown of a quantization is as wide as its widest entry: the numbers
// behind a quantization are the reason to open it, and the fixed width of
// 360 cut the longest line in half (ADR 321).
void TestTaskModelsTableQml::theQuantizationPopupIsAsWideAsItsWidestEntry()
{
    useManagedTasks();

    QQuickWindow window;
    window.resize(1040, 600);
    window.show();

    QQuickItem *host = new QQuickItem(window.contentItem());
    host->setSize(QSizeF(1040, 600));

    const std::unique_ptr<QObject> table = loadTable(host);
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QTest::qWait(20);

    const double widest = evaluate(table.get(), QStringLiteral("textWidth(quantItemText(FakeQuants[0]))")).toDouble();
    QVERIFY(widest > 0);
    const double wanted = evaluate(table.get(), QStringLiteral("quantPopupWidth(FakeQuants, quantColumnWidth)")).toDouble();
    QVERIFY(wanted >= widest);
    QVERIFY(wanted > 360.0);

    // The popup that opens really takes that width.
    QQuickItem *quant = comboBox(table.get(), QStringLiteral("id"));
    QVERIFY(quant);
    QObject *popup = quant->property("popup").value<QObject *>();
    QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTest::qWait(20);
    QCOMPARE(popup->property("width").toDouble(), wanted);
}

// The same table stands in the hardware step of the setup wizard, whose
// window is narrower than the «Models» window and which hides the runtime
// column: the row must stay inside it, and the field of a quantization must
// keep its width there too.
void TestTaskModelsTableQml::theColumnsFitTheNarrowWindowOfTheWizard()
{
    useManagedTasks();

    QQuickWindow window;
    window.resize(760, 600);
    window.show();

    QQuickItem *host = new QQuickItem(window.contentItem());
    host->setSize(QSizeF(720, 600));

    const std::unique_ptr<QObject> table = loadTable(host, false);
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QTest::qWait(20);

    QQuickItem *quant = comboBox(table.get(), QStringLiteral("id"));
    QVERIFY(quant);

    // The id and its marker fit even in the narrow window.
    QCOMPARE(quant->property("width").toDouble(), static_cast<double>(table->property("quantColumnWidth").toInt()));

    // The row stays inside the window: nothing is pushed out of sight.
    QVERIFY(quant->property("x").toDouble() + quant->property("width").toDouble() <= host->width());
}

// The settings window shows the runtime column, so it is another table: its
// row must fit the window of the settings as well.
void TestTaskModelsTableQml::theColumnsFitTheSettingsWindow()
{
    useManagedTasks();

    QQuickWindow window;
    window.resize(1040, 600);
    window.show();

    QQuickItem *host = new QQuickItem(window.contentItem());
    host->setSize(QSizeF(1040, 600));

    const std::unique_ptr<QObject> table = loadTable(host);
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QTest::qWait(20);

    QQuickItem *quant = comboBox(table.get(), QStringLiteral("id"));
    QVERIFY(quant);

    QCOMPARE(quant->property("width").toDouble(), static_cast<double>(table->property("quantColumnWidth").toInt()));
    QVERIFY(quant->property("x").toDouble() + quant->property("width").toDouble() <= host->width());

    // The switch of the runtime stands as high as the controls next to it.
    auto *root = qobject_cast<QQuickItem *>(table.get());
    QVERIFY(root);
    const QList<QQuickItem *> switches = itemsByType(root, QStringLiteral("SegmentedControl"));
    QCOMPARE(switches.size(), 4);
    for (QQuickItem *control : switches)
        QCOMPARE(control->property("height").toDouble(), 28.0);
}

// The gear opens the settings of its own task: it stands next to the fields
// of that task, and the free space of the row lies to the right of it.
void TestTaskModelsTableQml::theGearStandsNextToTheFields()
{
    useManagedTasks();

    QQuickWindow window;
    window.resize(1040, 600);
    window.show();

    QQuickItem *host = new QQuickItem(window.contentItem());
    host->setSize(QSizeF(1040, 600));

    const std::unique_ptr<QObject> table = loadTable(host);
    QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QTest::qWait(20);

    QQuickItem *quant = comboBox(table.get(), QStringLiteral("id"));
    QVERIFY(quant);
    QQuickItem *row = quant->parentItem();
    QVERIFY(row);

    const QList<QQuickItem *> gears = itemsByType(row, QStringLiteral("ToolButton"));
    QCOMPARE(gears.size(), 1);
    QQuickItem *gear = gears.first();

    const double gap = gear->property("x").toDouble() - (quant->property("x").toDouble() + quant->property("width").toDouble());
    QVERIFY(gap >= 0);
    QVERIFY(gap <= 8);

    // The free space is to the right of the gear, not between it and the
    // fields it belongs to.
    QVERIFY(host->width() - (gear->property("x").toDouble() + gear->property("width").toDouble()) > 100);
}

// The rows follow the runtime of each task: a managed task shows the
// quantization column, an external one the alias. The width measurements
// need the managed table.
void TestTaskModelsTableQml::useManagedTasks()
{
    for (const QString &role : connectionRoleIds())
        m_settings->setRuntimeModeForRole(role, QStringLiteral("managed"));
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
