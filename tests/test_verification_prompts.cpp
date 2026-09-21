#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QCoreApplication>

#include "app/SettingsStore.h"
#include "app/VerificationPromptStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

QString userPromptsPath(const QTemporaryDir &dir)
{
    return QDir(dir.path())
        .filePath(QStringLiteral("runtime/profiles/verifyPrompts.json"));
}

}  // namespace

class TestVerificationPrompts : public QObject {
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

    // --- Built-in defaults ---

    void builtInHasBlocksAndSystemPrompt()
    {
        auto store = makeStore();
        QVERIFY(store);
        QVERIFY(!store->systemPrompt().isEmpty());
        QVERIFY(store->systemPrompt().contains(QStringLiteral("FIX")));
        QVERIFY(!store->blockTypes().isEmpty());

        // Core block types the OCR model emits are present.
        const QStringList types = store->blockTypes();
        QVERIFY(types.contains(QStringLiteral("text")));
        QVERIFY(types.contains(QStringLiteral("title")));
        QVERIFY(types.contains(QStringLiteral("table")));
        QVERIFY(types.contains(QStringLiteral("equation")));

        // Each type has a prompt and a default enablement.
        QVERIFY(!store->promptForType(QStringLiteral("text")).isEmpty());
        QVERIFY(store->isTypeEnabled(QStringLiteral("text")));

        // The list model mirrors the store.
        const VerificationBlocksModel *model =
            dynamic_cast<VerificationBlocksModel *>(store->blockModel());
        QVERIFY(model);
        QVERIFY(model->rowCount() >= 6);
        QCOMPARE(model->rowCount(), store->blockTypes().size());
    }

    // --- User overrides are persisted and merged back on reload ---

    void savePersistsChangedPromptAndEnablement()
    {
        auto store = makeStore();
        QVERIFY(store);
        const QString userPath = userPromptsPath(m_dir);
        QVERIFY(!QFile::exists(userPath));

        const int textRow = store->blockTypes().indexOf(QStringLiteral("text"));
        const int titleRow = store->blockTypes().indexOf(QStringLiteral("title"));
        QVERIFY(textRow >= 0 && titleRow >= 0);

        const QString builtInTextPrompt =
            store->promptForType(QStringLiteral("text"));
        QVERIFY(!builtInTextPrompt.isEmpty());

        auto *model =
            dynamic_cast<VerificationBlocksModel *>(store->blockModel());
        QVERIFY(model);
        model->setEnabled(titleRow, false);
        model->setPrompt(textRow, QStringLiteral("Custom prompt for text."));
        store->setSystemPrompt(QStringLiteral("Custom system prompt."));

        store->save();
        QVERIFY(QFile::exists(userPath));

        // A second store instance (fresh process) must see the overrides.
        auto settings2 = makeSettings();
        QVERIFY(settings2);
        VerificationPromptStore reloaded(*settings2);
        QVERIFY(!reloaded.isTypeEnabled(QStringLiteral("title")));
        QCOMPARE(reloaded.promptForType(QStringLiteral("text")),
                 QStringLiteral("Custom prompt for text."));
        QCOMPARE(reloaded.systemPrompt(), QStringLiteral("Custom system prompt."));

        // Unchanged types keep their built-in values.
        QVERIFY(reloaded.isTypeEnabled(QStringLiteral("table")));
        QVERIFY(!reloaded.promptForType(QStringLiteral("table")).isEmpty());

        // Restoring defaults removes the user file and re-reads built-ins.
        reloaded.resetToDefaults();
        QVERIFY(reloaded.isTypeEnabled(QStringLiteral("title")));
        QCOMPARE(reloaded.promptForType(QStringLiteral("text")),
                 builtInTextPrompt);
    }

    // --- Saving the built-in state removes the user file ---

    void saveMatchingDefaultsRemovesUserFile()
    {
        auto store = makeStore();
        QVERIFY(store);
        // Nothing changed; save() must not leave a user file behind.
        store->save();
        QVERIFY(!QFile::exists(userPromptsPath(m_dir)));
    }
};

QTEST_MAIN(TestVerificationPrompts)

#include "test_verification_prompts.moc"