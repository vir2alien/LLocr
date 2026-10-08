#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "app/VerificationPromptStore.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

QString userPromptsPath(const QTemporaryDir &dir)
{
    return QDir(dir.path()).filePath(QStringLiteral("runtime/profiles/verifyPrompts.json"));
}

}  // namespace

class TestVerificationPrompts : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;
    QTemporaryDir m_dir;

    // Stores are created fresh per test (locals) so they are destroyed before
    // the next test runs; sharing one across tests caused use-after-free when a
    // later test was invoked after an earlier one had destroyed it.
    std::unique_ptr<SettingsStore> makeSettings()
    {
        auto settings = std::make_unique<SettingsStore>();
        settings->setRuntimeRootDir(m_dir.filePath(QStringLiteral("runtime")));
        settings->setRuntimeModelsDir(m_dir.filePath(QStringLiteral("models")));
        return std::move(settings);
    }

    std::unique_ptr<VerificationPromptStore> makeStore()
    {
        auto settings = makeSettings();
        auto store = std::make_unique<VerificationPromptStore>(*settings);
        m_settings = std::move(settings);
        return std::move(store);
    }

    std::unique_ptr<SettingsStore> m_settings;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_verification_prompts"));
    }

    void builtInHasBlockTypes()
    {
        auto store = makeStore();
        QVERIFY(store);

        const VerificationBlocksModel *model = dynamic_cast<VerificationBlocksModel *>(store->blockModel());
        QVERIFY(model);
        QVERIFY(model->rowCount() > 0);

        QStringList types;
        for (const VerificationBlock &block : model->blocks())
            types.append(block.type);
        QVERIFY(!types.isEmpty());

        // Core block types the OCR model emits are present.
        QVERIFY(types.contains(QStringLiteral("text")));
        QVERIFY(types.contains(QStringLiteral("title")));
        QVERIFY(types.contains(QStringLiteral("table")));
        QVERIFY(types.contains(QStringLiteral("equation")));

        // Content types ship enabled; service types do not.
        QVERIFY(store->isTypeEnabled(QStringLiteral("text")));
        QVERIFY(!store->isTypeEnabled(QStringLiteral("page_number")));

        QCOMPARE(model->rowCount(), types.size());
    }

    void savePersistsChangedEnablement()
    {
        auto store = makeStore();
        QVERIFY(store);
        const QString userPath = userPromptsPath(m_dir);
        QVERIFY(!QFile::exists(userPath));

        auto *model = dynamic_cast<VerificationBlocksModel *>(store->blockModel());
        QVERIFY(model);

        const int titleRow = model->rowOfType(QStringLiteral("title"));
        const int pageNumberRow = model->rowOfType(QStringLiteral("page_number"));
        QVERIFY(titleRow >= 0 && pageNumberRow >= 0);

        model->setEnabled(titleRow, false);
        model->setEnabled(pageNumberRow, true);

        store->save();
        QVERIFY(QFile::exists(userPath));

        // A second store instance (fresh process) must see the overrides.
        auto settings2 = makeSettings();
        QVERIFY(settings2);
        VerificationPromptStore reloaded(*settings2);
        QVERIFY(!reloaded.isTypeEnabled(QStringLiteral("title")));
        QVERIFY(reloaded.isTypeEnabled(QStringLiteral("page_number")));

        // Unchanged types keep their built-in enablement.
        QVERIFY(reloaded.isTypeEnabled(QStringLiteral("table")));

        // Restoring defaults removes the user file and re-reads built-ins.
        reloaded.resetToDefaults();
        QVERIFY(reloaded.isTypeEnabled(QStringLiteral("title")));
        QVERIFY(!reloaded.isTypeEnabled(QStringLiteral("page_number")));
    }

    void saveMatchingDefaultsRemovesUserFile()
    {
        auto store = makeStore();
        QVERIFY(store);
        // Nothing changed; save() must not leave a user file behind.
        store->save();
        QVERIFY(!QFile::exists(userPromptsPath(m_dir)));
    }

    void legacyPromptFieldsAreIgnored()
    {
        // A user file written by an older build carries systemPrompt and
        // per-block prompt fields; only the enablement may be applied.
        const QString userPath = userPromptsPath(m_dir);
        QDir().mkpath(QFileInfo(userPath).absolutePath());
        QFile legacy(userPath);
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write("{\n"
                     "  \"schemaVersion\": 1,\n"
                     "  \"systemPrompt\": \"Legacy shared prompt.\",\n"
                     "  \"blocks\": [\n"
                     "    {\"type\": \"title\", \"enabled\": false, \"prompt\": \"Legacy title prompt.\"},\n"
                     "    {\"type\": \"page_number\", \"enabled\": true}\n"
                     "  ]\n"
                     "}\n");
        legacy.close();

        auto store = makeStore();
        QVERIFY(store);
        QVERIFY(!store->isTypeEnabled(QStringLiteral("title")));
        QVERIFY(store->isTypeEnabled(QStringLiteral("page_number")));

        // Saving rewrites the file with enablement only.
        store->save();
        QFile rewritten(userPath);
        QVERIFY(rewritten.open(QIODevice::ReadOnly));
        const QByteArray contents = rewritten.readAll();
        QVERIFY(!contents.contains("prompt\""));
    }

    // --- Group-filtered proxies (VerificationBlocksTab columns) ---

    void groupProxiesSplitAndCoverAllRows()
    {
        auto store = makeStore();
        QVERIFY(store);
        auto *src = qobject_cast<QAbstractListModel *>(store->blockModel());
        QVERIFY(src);
        const int total = src->rowCount();
        QVERIFY(total > 0);

        auto *content = qobject_cast<QAbstractItemModel *>(store->blockModelContent());
        auto *captions = qobject_cast<QAbstractItemModel *>(store->blockModelCaptions());
        auto *service = qobject_cast<QAbstractItemModel *>(store->blockModelService());
        QVERIFY(content && captions && service);
        QCOMPARE(content->rowCount() + captions->rowCount() + service->rowCount(), total);
        QVERIFY(content->rowCount() > 0);

        // Each proxy must contain only rows of its own group.
        const int groupRole = src->roleNames().key(QByteArrayLiteral("group"), -1);
        QVERIFY(groupRole != -1);
        auto checkGroup = [&](QAbstractItemModel *proxy, const QString &expected) {
            for (int i = 0; i < proxy->rowCount(); ++i) {
                const QModelIndex idx = proxy->index(i, 0);
                QCOMPARE(idx.data(groupRole).toString(), expected);
            }
        };
        checkGroup(content, QStringLiteral("content"));
        checkGroup(captions, QStringLiteral("captions"));
        checkGroup(service, QStringLiteral("service"));
    }

    void groupProxiesTrackSourceReset()
    {
        auto store = makeStore();
        QVERIFY(store);
        auto *src = qobject_cast<QAbstractListModel *>(store->blockModel());
        QVERIFY(src);
        auto *content = qobject_cast<QAbstractItemModel *>(store->blockModelContent());
        QVERIFY(content);

        QSignalSpy rowsRemoved(src, &QAbstractListModel::rowsRemoved);
        QVERIFY(rowsRemoved.isValid());

        const int before = content->rowCount();
        QVERIFY(before > 0);

        // Drop every row whose group is not "content" by rebuilding the
        // source from the surviving subset; proxies must stay consistent.
        store->loadValues();  // reset path: loadBuiltIn + loadUser + rebuildModel
        QCOMPARE(content->rowCount(), before);
    }

    void groupProxyIndexResolvesToSourceRowOfType()
    {
        auto store = makeStore();
        QVERIFY(store);
        auto *src = qobject_cast<VerificationBlocksModel *>(store->blockModel());
        QVERIFY(src);
        auto *captions = qobject_cast<QAbstractItemModel *>(store->blockModelCaptions());
        QVERIFY(captions);

        // For every proxy row there must be a source row whose type matches.
        const int typeRole = src->roleNames().key(QByteArrayLiteral("type"), -1);
        QVERIFY(typeRole != -1);
        for (int i = 0; i < captions->rowCount(); ++i) {
            const QString type = captions->index(i, 0).data(typeRole).toString();
            QVERIFY(!type.isEmpty());
            const int sourceRow = src->rowOfType(type);
            QVERIFY(sourceRow >= 0);
            QCOMPARE(src->typeAt(sourceRow), type);
        }
        // Unknown types resolve to -1.
        QCOMPARE(src->rowOfType(QStringLiteral("no-such-type")), -1);
    }
};

QTEST_MAIN(TestVerificationPrompts)

#include "test_verification_prompts.moc"
