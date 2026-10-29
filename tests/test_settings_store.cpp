#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QSettings>
#include <QtTest>

#include "config/SettingsStore.h"
#include "core/ConnectionRole.h"
#include "core/ModelProfiles.h"
#include "testsettings.h"

using namespace llocr;

namespace {

struct ConnectionsBlock {
    QString target;
    QStringList handlers;
};

// A Connections block runs to its matching brace; the target and every handler
// inside it belong together, which is what a flat regex gets wrong.
QList<ConnectionsBlock> parseConnectionsBlocks(const QString &source)
{
    static const QRegularExpression start(QStringLiteral("\\bConnections\\s*\\{"));
    static const QRegularExpression targetRe(QStringLiteral("\\btarget:\\s*([A-Za-z_]\\w*)"));
    static const QRegularExpression handlerRe(QStringLiteral("\\bfunction\\s+on([A-Z]\\w*Changed)\\s*\\("));

    QList<ConnectionsBlock> blocks;
    int from = 0;
    while (true) {
        const QRegularExpressionMatch open = start.match(source, from);
        if (!open.hasMatch())
            break;
        int depth = 0;
        int i = open.capturedEnd() - 1;
        for (; i < source.size(); ++i) {
            if (source.at(i) == QLatin1Char('{'))
                ++depth;
            else if (source.at(i) == QLatin1Char('}') && --depth == 0)
                break;
        }
        const QString body = source.mid(open.capturedEnd(), i - open.capturedEnd());

        ConnectionsBlock block;
        const QRegularExpressionMatch target = targetRe.match(body);
        if (target.hasMatch()) {
            block.target = target.captured(1);
            QRegularExpressionMatchIterator it = handlerRe.globalMatch(body);
            while (it.hasNext())
                block.handlers.append(it.next().captured(1));
            if (!block.handlers.isEmpty())
                blocks.append(block);
        }
        from = i + 1;
    }
    return blocks;
}

QStringList findQmlFiles(const QDir &root)
{
    QStringList out;
    for (const QFileInfo &entry : root.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.isDir())
            out += findQmlFiles(QDir(entry.absoluteFilePath()));
        else if (entry.suffix() == QLatin1String("qml"))
            out.append(entry.absoluteFilePath());
    }
    return out;
}

}  // namespace

class TestSettingsStore : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_settings"));
    }

    void cleanup()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
        m_settingsIsolation.clearSecrets();
    }

    void defaultValues()
    {
        SettingsStore store;
        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
        QCOMPARE(store.apiKey(), QStringLiteral(""));
        QCOMPARE(store.connectionTimeoutMs(), 5000);
        QCOMPARE(store.responseTimeoutMs(), 300000);
        QCOMPARE(store.modelName(), QStringLiteral("MinerU2.5-Pro-2605-1.2B"));
        QCOMPARE(store.decisionModelName(), QString());
        // "auto" — the OCR model adapter declares its parser (ADR 88).
        QCOMPARE(store.parserId(), QStringLiteral("auto"));
        QCOMPARE(store.decisionMatchThreshold(), 0.5);
        QCOMPARE(store.themeMode(), 0);
        QCOMPARE(store.language(), QStringLiteral("system"));
        QCOMPARE(store.splitPages(), true);
        QCOMPARE(store.keepPageNumbers(), true);
        QCOMPARE(store.pdfLandscape(), false);
        QCOMPARE(store.pdfMarginMm(), 15);
    }

    void resetToDefaults()
    {
        SettingsStore store;
        store.setBaseUrl(QStringLiteral("http://custom:1234"));
        store.setApiKey(QStringLiteral("secret-token"));
        store.setConnectionTimeoutMs(8000);
        store.setResponseTimeoutMs(60000);
        store.setModelName(QStringLiteral("custom-model"));
        store.setParserId(QStringLiteral("raw"));
        store.setThemeMode(1);
        store.setLanguage(QStringLiteral("ru"));

        store.resetToDefaults();

        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
        QCOMPARE(store.connectionTimeoutMs(), 5000);
        QCOMPARE(store.responseTimeoutMs(), 300000);
        QCOMPARE(store.modelName(), QStringLiteral("MinerU2.5-Pro-2605-1.2B"));
        // "auto" — the OCR model adapter declares its parser (ADR 88).
        QCOMPARE(store.parserId(), QStringLiteral("auto"));
        QCOMPARE(store.themeMode(), 0);
        QCOMPARE(store.language(), QStringLiteral("system"));
        // The secrets are not settings: they live in the OS store and survive
        // the reset (ADR 227).
        QCOMPARE(store.apiKey(), QStringLiteral("secret-token"));
    }

    // The external connection of one task (ADR 226): an empty field means the
    // shared value.
    void taskEndpointFallsBackToTheSharedOne()
    {
        SettingsStore store;
        store.setBaseUrl(QStringLiteral("http://shared:8080"));

        QCOMPARE(store.baseUrlForRole(QStringLiteral("decision")), QString());
        QCOMPARE(store.effectiveBaseUrlForRole(QStringLiteral("decision")), QStringLiteral("http://shared:8080"));

        store.setBaseUrlForRole(QStringLiteral("decision"), QStringLiteral("http://own:9000"));
        QCOMPARE(store.baseUrlForRole(QStringLiteral("decision")), QStringLiteral("http://own:9000"));
        QCOMPARE(store.effectiveBaseUrlForRole(QStringLiteral("decision")), QStringLiteral("http://own:9000"));
        // The other tasks keep the shared one.
        QCOMPARE(store.effectiveBaseUrlForRole(QStringLiteral("fix")), QStringLiteral("http://shared:8080"));
    }

    void taskKeyFallsBackToTheSharedOne()
    {
        SettingsStore store;
        QVERIFY(!store.apiKeySet());
        QVERIFY(!store.apiKeySetForRole(QStringLiteral("fix")));

        store.setApiKey(QStringLiteral("shared-key"));
        QVERIFY(store.apiKeySet());
        QCOMPARE(store.apiKeyForRole(QStringLiteral("fix")), QStringLiteral("shared-key"));

        store.setApiKeyForRole(QStringLiteral("fix"), QStringLiteral("own-key"));
        QVERIFY(store.apiKeySetForRole(QStringLiteral("fix")));
        QCOMPARE(store.apiKeyForRole(QStringLiteral("fix")), QStringLiteral("own-key"));
        // The task without its own key still uses the shared one.
        QCOMPARE(store.apiKeyForRole(QStringLiteral("decision")), QStringLiteral("shared-key"));

        store.clearApiKeyForRole(QStringLiteral("fix"));
        QVERIFY(!store.apiKeySetForRole(QStringLiteral("fix")));
        QCOMPARE(store.apiKeyForRole(QStringLiteral("fix")), QStringLiteral("shared-key"));
    }

    // The reason the store refused has to reach the UI, and the UI only
    // learns about it when the store says so (ADR 233).
    void theSecretStateIsToldToTheUi()
    {
        SettingsStore store;
        QSignalSpy secretsSpy(&store, &SettingsStore::secretsChanged);

        store.refreshSecretState();
        QCOMPARE(secretsSpy.count(), 1);

        // Deleting a key that is not there still tells the UI: the store may
        // have refused to delete it.
        store.clearApiKey();
        QCOMPARE(secretsSpy.count(), 2);
    }

    // Scoped resets (ADR 75): only the window's own group returns to
    // defaults; everything else stays as the user configured it.
    void resetOutputGroup()
    {
        SettingsStore store;
        store.setParserId(QStringLiteral("raw"));
        store.setSplitPages(false);
        store.setKeepPageNumbers(false);
        store.setPdfLandscape(true);
        store.setPdfMarginMm(40);
        store.setBaseUrl(QStringLiteral("http://custom:1"));
        store.setLanguage(QStringLiteral("ru"));

        store.resetOutputDefaults();

        // "auto" — the OCR model adapter declares its parser (ADR 88).
        QCOMPARE(store.parserId(), QStringLiteral("auto"));
        QCOMPARE(store.splitPages(), true);
        QCOMPARE(store.keepPageNumbers(), true);
        QCOMPARE(store.pdfLandscape(), false);
        QCOMPARE(store.pdfMarginMm(), 15);
        // Outside the group — untouched.
        QCOMPARE(store.baseUrl(), QStringLiteral("http://custom:1"));
        QCOMPARE(store.language(), QStringLiteral("ru"));
    }

    void resetRuntimeGroup()
    {
        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setBaseUrl(QStringLiteral("http://custom:9000"));
        store.setApiKey(QStringLiteral("k"));
        store.setConnectionTimeoutMs(8000);
        store.setResponseTimeoutMs(60000);
        store.setModelName(QStringLiteral("ocr-model"));
        store.setDecisionModelName(QStringLiteral("decision-model"));
        store.setServerPath(QStringLiteral("/opt/llama-server"));
        store.setThemeMode(1);

        store.resetRuntimeDefaults();

        QCOMPARE(store.connectionMode(), QStringLiteral("external"));
        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
        QCOMPARE(store.apiKey(), QString());
        QCOMPARE(store.connectionTimeoutMs(), 5000);
        QCOMPARE(store.responseTimeoutMs(), 300000);
        QCOMPARE(store.modelName(), QStringLiteral("MinerU2.5-Pro-2605-1.2B"));
        QCOMPARE(store.decisionModelName(), QString());
        QCOMPARE(store.serverPath(), QString());
        // Outside the group — untouched.
        QCOMPARE(store.themeMode(), 1);
        QCOMPARE(store.language(), QStringLiteral("system"));
    }

    void outputExportKeysRoundTrip()
    {
        SettingsStore store;
        store.setSplitPages(false);
        store.setKeepPageNumbers(false);
        store.setTablesAsHtml(true);
        store.setPdfLandscape(true);
        store.setPdfMarginMm(25);
        QCOMPARE(store.splitPages(), false);
        QCOMPARE(store.keepPageNumbers(), false);
        QCOMPARE(store.tablesAsHtml(), true);
        QCOMPARE(store.pdfLandscape(), true);
        QCOMPARE(store.pdfMarginMm(), 25);

        // The margin is clamped to the sane range.
        store.setPdfMarginMm(500);
        QCOMPARE(store.pdfMarginMm(), 50);
        store.setPdfMarginMm(-3);
        QCOMPARE(store.pdfMarginMm(), 0);

        store.resetToDefaults();
        QCOMPARE(store.splitPages(), true);
        QCOMPARE(store.keepPageNumbers(), true);
        QCOMPARE(store.tablesAsHtml(), false);
        QCOMPARE(store.pdfLandscape(), false);
        QCOMPARE(store.pdfMarginMm(), 15);
    }

    void newRuntimeDefaults()
    {
        SettingsStore store;
        QCOMPARE(store.connectionMode(), QStringLiteral("external"));
        QCOMPARE(store.setupVersion(), 0);
        QCOMPARE(store.setupDismissed(), false);
        QCOMPARE(store.serverPath(), QStringLiteral(""));
        QCOMPARE(store.autoStart(), false);  // §4.3: off by default
        QCOMPARE(store.startOnDemand(), true);
        QCOMPARE(store.stopOnExit(), true);
        QCOMPARE(store.autoRestart(), true);
        QCOMPARE(store.startupTimeoutMs(), 180000);
        QCOMPARE(store.allowNonLoopback(), false);

        QCOMPARE(store.launchHost(), QStringLiteral("127.0.0.1"));
        QCOMPARE(store.launchPort(), 0);
        QCOMPARE(store.launchProfileId(), QString());
        QCOMPARE(store.hfToken(), QString());
    }

    void timeoutAndPortClamps()
    {
        SettingsStore store;
        store.setConnectionTimeoutMs(0);
        QCOMPARE(store.connectionTimeoutMs(), 1000);
        store.setConnectionTimeoutMs(-500);
        QCOMPARE(store.connectionTimeoutMs(), 1000);
        store.setConnectionTimeoutMs(10 * 60 * 1000 + 1);
        QCOMPARE(store.connectionTimeoutMs(), 600000);
        store.setConnectionTimeoutMs(8000);
        QCOMPARE(store.connectionTimeoutMs(), 8000);

        store.setResponseTimeoutMs(0);
        QCOMPARE(store.responseTimeoutMs(), 1000);
        store.setResponseTimeoutMs(2 * 60 * 60 * 1000);
        QCOMPARE(store.responseTimeoutMs(), 3600000);
        store.setResponseTimeoutMs(60000);
        QCOMPARE(store.responseTimeoutMs(), 60000);

        store.setStartupTimeoutMs(-1);
        QCOMPARE(store.startupTimeoutMs(), 1000);
        store.setStartupTimeoutMs(7200000);
        QCOMPARE(store.startupTimeoutMs(), 3600000);
        store.setStartupTimeoutMs(10000);
        QCOMPARE(store.startupTimeoutMs(), 10000);

        // 0 stays 0: it means auto-pick.
        store.setLaunchPort(-1);
        QCOMPARE(store.launchPort(), 0);
        store.setLaunchPort(70000);
        QCOMPARE(store.launchPort(), 65535);
        store.setLaunchPort(8081);
        QCOMPARE(store.launchPort(), 8081);

        store.resetToDefaults();
        QCOMPARE(store.connectionTimeoutMs(), 5000);
        QCOMPARE(store.responseTimeoutMs(), 300000);
        QCOMPARE(store.startupTimeoutMs(), 180000);
        QCOMPARE(store.launchPort(), 0);
    }

    // The check role's request profile is the id of its model profile. A
    // profile written before the split has nothing stored, and the verifier
    // would resolve to no sampling parameters at all.
    // The app's four tasks, in one list: the settings dispatch, the runtime's
    // per-task state and the QML task table read it (ADR 215).
    void theTaskListIsTheSingleSource()
    {
        QCOMPARE(connectionRoleIds(), QStringList({QStringLiteral("blockRecognition"), QStringLiteral("decision"), QStringLiteral("layout"), QStringLiteral("fix")}));
        static_assert(connectionRoleCount == 4);

        SettingsStore store;
        QCOMPARE(store.taskRoles(), connectionRoleIds());

        // No alias and no page tasks: anything that is not exactly a role id
        // falls back to the block recognition task, and the round-trip drops
        // it — that is what the install queue's filter relies on.
        QCOMPARE(connectionRoleFromName(QStringLiteral("ocr")), ConnectionRole::BlockRecognition);
        QCOMPARE(connectionRoleFromName(QStringLiteral("pageOcr")), ConnectionRole::BlockRecognition);
        QCOMPARE(connectionRoleFromName(QStringLiteral("unknown")), ConnectionRole::BlockRecognition);
        for (const QString &id : connectionRoleIds())
            QCOMPARE(connectionRoleName(connectionRoleFromName(id)), id);
    }

    // Every task's settings are its own: the dispatch writes to the role's
    // own key and reads it back (ADR 216).
    void theRoleDispatchReachesEveryRole()
    {
        SettingsStore store;
        for (const QString &id : connectionRoleIds()) {
            store.setModelNameForRole(id, QStringLiteral("name-%1").arg(id));
            store.setLaunchModelPathForRole(id, QStringLiteral("/models/%1.gguf").arg(id));
            store.setLaunchMmprojPathForRole(id, QStringLiteral("/models/%1-mmproj.gguf").arg(id));
            store.setLaunchDraftPathForRole(id, QStringLiteral("/models/%1-draft.gguf").arg(id));
        }
        for (const QString &id : connectionRoleIds()) {
            QCOMPARE(store.modelNameForRole(id), QStringLiteral("name-%1").arg(id));
            QCOMPARE(store.launchModelPathForRole(id), QStringLiteral("/models/%1.gguf").arg(id));
            QCOMPARE(store.launchMmprojPathForRole(id), QStringLiteral("/models/%1-mmproj.gguf").arg(id));
            QCOMPARE(store.launchDraftPathForRole(id), QStringLiteral("/models/%1-draft.gguf").arg(id));
        }

        // The request profile of one task is that task's own: the dispatch
        // must not read another role's. The block recognition task keeps its
        // identity in the model recipe id.
        store.setModelRecipeId(QStringLiteral("block-profile"));
        QCOMPARE(store.requestProfileIdForRole(QStringLiteral("blockRecognition")), QStringLiteral("block-profile"));
        QVERIFY(store.requestProfileIdForRole(QStringLiteral("decision")) != QStringLiteral("block-profile"));
        QVERIFY(store.requestProfileIdForRole(QStringLiteral("fix")) != QStringLiteral("block-profile"));
    }

    void checkRoleResolvesAModelProfileWithoutOneStored()
    {
        QSettings pre;
        pre.remove(QStringLiteral("model/recipeId"));

        const SettingsStore store;
        QCOMPARE(store.requestProfileIdForRole(QStringLiteral("blockRecognition")), QString::fromUtf8(SettingsStore::kDefaultModelRecipeId));
        // Nothing is written: the default is a fallback, not a stored choice.
        QVERIFY(!store.contains(QStringLiteral("model/recipeId")));

        // An explicit choice still wins.
        SettingsStore chosen;
        chosen.setModelRecipeId(QStringLiteral("qwen3.5-4b"));
        QCOMPARE(chosen.requestProfileIdForRole(QStringLiteral("blockRecognition")), QStringLiteral("qwen3.5-4b"));
    }

    // The key's default used to be the empty string, so every existing profile
    // holds one. QSettings::value() would return that empty string instead of
    // Same fallback rule as the check role: the decision profile id is the id
    // of its model profile, and nothing is stored until the user picks one.
    void decisionRoleResolvesAModelProfileWithoutOneStored()
    {
        QSettings pre;
        pre.remove(QStringLiteral("tasks/decision/requestProfileId"));

        const SettingsStore store;
        const QStringList decisionModels = ModelProfiles::idsForRole(ModelProfiles::instance(), QStringLiteral("decision"));
        QVERIFY(!decisionModels.isEmpty());
        QCOMPARE(store.decisionRequestProfileId(), decisionModels.constFirst());
        QVERIFY(!store.contains(QStringLiteral("tasks/decision/requestProfileId")));

        SettingsStore chosen;
        chosen.setDecisionRequestProfileId(decisionModels.constFirst());
        QCOMPARE(chosen.decisionRequestProfileId(), decisionModels.constFirst());
    }

    // The layout role resolves like the others: the role's recommended model
    // until the user stores a pick of its own, and an activation for the role
    // writes the layout key — not the check one it followed before ADR 150.
    void layoutRoleResolvesAModelProfileWithoutOneStored()
    {
        QSettings pre;
        pre.remove(QStringLiteral("tasks/layout/requestProfileId"));

        const SettingsStore store;
        QCOMPARE(store.layoutRequestProfileId(), ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("layout")));
        QVERIFY(!store.contains(QStringLiteral("tasks/layout/requestProfileId")));

        SettingsStore chosen;
        chosen.setModelRecipeId(QStringLiteral("glm-ocr"));
        chosen.selectModelProfile(QStringLiteral("mradermacher/MinerU2.5-Pro-2605-1.2B-GGUF"), QStringLiteral("layout"));
        QCOMPARE(chosen.layoutRequestProfileId(), QStringLiteral("mineru"));
        QCOMPARE(chosen.modelRecipeId(), QStringLiteral("glm-ocr"));
    }

    // The threshold arrives from a 0–100 spin box and an INI that can be
    // hand-edited: values outside [0, 1] are clamped wherever they come from.
    void decisionMatchThresholdClampsAndRoundTrips()
    {
        SettingsStore store;
        QCOMPARE(store.decisionMatchThreshold(), 0.5);

        store.setDecisionMatchThreshold(0.75);
        QCOMPARE(store.decisionMatchThreshold(), 0.75);
        store.setDecisionMatchThreshold(0.75);  // no-op, no signal spam

        store.setDecisionMatchThreshold(-1.0);
        QCOMPARE(store.decisionMatchThreshold(), 0.0);
        store.setDecisionMatchThreshold(2.0);
        QCOMPARE(store.decisionMatchThreshold(), 1.0);

        QSettings pre;
        pre.setValue(QStringLiteral("decision/matchThreshold"), 7.0);
        const SettingsStore reopened;
        QCOMPARE(reopened.decisionMatchThreshold(), 1.0);
    }

    // Activating a model of a known family has to pick that family's profile:
    // the weights on disk would otherwise run with another model's launch
    // parameters. A repo the catalog does not know, or one that does not answer
    // the role, leaves the selection alone.
    void activatingAModelSelectsItsFamilyProfile()
    {
        SettingsStore store;
        store.setModelRecipeId(QStringLiteral("mineru"));
        store.setFixRequestProfileId(QStringLiteral("gemma-4-e4b"));

        // GLM-OCR answers the block recognition task — it ships the block
        // transcription — so activation for it follows its own profile.
        store.selectModelProfile(QStringLiteral("ggml-org/GLM-OCR-GGUF"), QStringLiteral("blockRecognition"));
        QCOMPARE(store.modelRecipeId(), QStringLiteral("glm-ocr"));

        store.selectModelProfile(QStringLiteral("unsloth/Qwen3.5-4B-MTP-GGUF"), QStringLiteral("fix"));
        QCOMPARE(store.fixRequestProfileId(), QStringLiteral("qwen3.5-4b"));

        // A family that does not answer the task leaves the selection alone:
        // the layout family is no fix model, the fix-only families are no
        // block models.
        store.selectModelProfile(QStringLiteral("mradermacher/MinerU2.5-Pro-2605-1.2B-GGUF"), QStringLiteral("fix"));
        QCOMPARE(store.fixRequestProfileId(), QStringLiteral("qwen3.5-4b"));
        store.selectModelProfile(QStringLiteral("unsloth/Qwen3.5-4B-MTP-GGUF"), QStringLiteral("blockRecognition"));
        QCOMPARE(store.modelRecipeId(), QStringLiteral("glm-ocr"));

        // A hand-picked GGUF joins to nothing.
        store.setModelRecipeId(QStringLiteral("some-gguf"));
        store.selectModelProfile(QString(), QStringLiteral("blockRecognition"));
        store.selectModelProfile(QStringLiteral("someone/Their-Model-GGUF"), QStringLiteral("blockRecognition"));
        QCOMPARE(store.modelRecipeId(), QStringLiteral("some-gguf"));
    }

    // qmllint cannot resolve Settings (it is registered by hand in main.cpp, not
    // a module type), so a handler for a signal that no longer exists is
    // invisible to the lint gate and surfaces only as a runtime warning. This
    // walks the QML, pairs each Connections block with its target, and asks the
    // meta-object of the targets this test can see.
    void qmlSignalHandlersExist()
    {
        const QDir root(QStringLiteral(LLOCR_SOURCE_DIR) + QStringLiteral("/src/qml"));
        QVERIFY2(root.exists(), qPrintable(root.path()));

        // Only Settings is linked here; the other singletons live in layers this
        // target does not pull in, so their blocks are skipped rather than
        // reported as missing.
        const QHash<QString, const QMetaObject *> known = {{QStringLiteral("Settings"), &SettingsStore::staticMetaObject}};

        int blocks = 0;
        int handlers = 0;
        for (const QString &path : findQmlFiles(root)) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString source = QString::fromUtf8(file.readAll());
            for (const ConnectionsBlock &block : parseConnectionsBlocks(source)) {
                const QMetaObject *mo = known.value(block.target);
                if (!mo)
                    continue;
                ++blocks;
                for (const QString &handler : block.handlers) {
                    // onLaunchModelPathChanged names the signal
                    // launchModelPathChanged; indexOfSignal() wants a signature.
                    const QString signature = handler.left(1).toLower() + handler.mid(1) + QLatin1String("()");
                    QVERIFY2(mo->indexOfSignal(signature.toUtf8().constData()) >= 0, qPrintable(QStringLiteral("%1: %2 has no signal %3").arg(path, block.target, handler)));
                    ++handlers;
                }
            }
        }
        QVERIFY2(blocks > 0, "no Connections block targets a singleton this test can check");
        QVERIFY(handlers > 0);
    }

    // The reset runs once: the version it writes makes the next construction
    // skip it, so the values set after the first run survive.
    void theResetRunsOnce()
    {
        {
            SettingsStore store;
            store.setSetupVersion(1);
            store.setConnectionMode(QStringLiteral("managed"));
        }
        {
            SettingsStore store;
            QCOMPARE(store.setupVersion(), 1);
            QCOMPARE(store.connectionMode(), QStringLiteral("managed"));
        }
    }

    void resetTableEntriesAreValid()
    {
        // Every row in the defaults table (review 3.5) must reference a real,
        // writable Q_PROPERTY, and its default must match what the getter
        // returns on a fresh store — otherwise resetToDefaults() drifts from
        // the property declarations.
        SettingsStore store;
        const QMetaObject *mo = store.metaObject();
        QVERIFY(SettingsStore::defaultsCount() > 0);
        for (int i = 0; i < SettingsStore::defaultsCount(); ++i) {
            const SettingsStore::SettingDefault &entry = SettingsStore::defaults()[i];
            const QMetaProperty prop = mo->property(mo->indexOfProperty(entry.property));
            QVERIFY2(prop.isValid(), entry.property);
            QVERIFY2(prop.isWritable(), entry.property);
            QCOMPARE(store.property(entry.property), entry.defaultValue);
        }
    }

    void resetCoversEveryDeclaredProperty()
    {
        // Drift guard: set every writable property to a sentinel value, reset,
        // and verify none of the sentinels survive. A property that is added
        // but forgotten in kDefaults would stay at its sentinel and fail here.
        SettingsStore store;
        const QMetaObject *mo = store.metaObject();

        // Properties excluded from reset by design:
        //  - windowX/Y/Width/Height/State: UI state, intentionally not reset;
        //  - runtimeRootDir / runtimeModelsDir: they point at the user's disk,
        //    not at the product's schema — the reset carries them over;
        //  - hfToken: the token lives in the OS secret store (ADR 227), which
        //    the reset does not touch;
        //  - connectionMode / lastExternalBaseUrl: covered implicitly through
        //    setConnectionMode()'s restore path (baseUrl is reset, but its
        //    final value when coming from Managed is the restored endpoint).
        auto excluded = [](const QByteArray &name) {
            return name.startsWith("window") || name == QByteArrayLiteral("connectionMode") || name == QByteArrayLiteral("lastExternalBaseUrl") || name == QByteArrayLiteral("runtimeRootDir") ||
                   name == QByteArrayLiteral("runtimeModelsDir") || name == QByteArrayLiteral("hfToken");
        };

        QHash<QByteArray, QVariant> sentinels;
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QMetaProperty prop = mo->property(i);
            if (!prop.isWritable() || excluded(prop.name()))
                continue;

            // A sentinel guaranteed to differ from the property's current
            // (default) value.
            QVariant sentinel;
            switch (prop.metaType().id()) {
            case QMetaType::QString:
                sentinel = QVariant(QStringLiteral("\u00A7sentinel\u00A7"));
                break;
            case QMetaType::Int:
                sentinel = QVariant(1000000);
                break;
            case QMetaType::Double:
                sentinel = QVariant(12345.678);
                break;
            case QMetaType::Bool:
                sentinel = QVariant(!store.property(prop.name()).toBool());
                break;
            default:
                QFAIL("unexpected property type in SettingsStore");
            }
            QVERIFY2(prop.write(&store, sentinel), prop.name());
            sentinels.insert(prop.name(), sentinel);
        }
        QVERIFY(sentinels.size() > 0);

        store.resetToDefaults();

        for (auto it = sentinels.cbegin(); it != sentinels.cend(); ++it) {
            QVERIFY2(store.property(it.key()) != it.value(), it.key());
        }
    }

    // A settings file from before the reset carries no current schema version:
    // it is wiped once, and the version the reset writes makes the next start
    // skip it.
    void aStaleSchemaVersionResetsTheSettings()
    {
        QSettings pre;
        pre.setValue(QStringLiteral("model/recipeId"), QStringLiteral("glm-ocr"));
        pre.setValue(QStringLiteral("settings/schemaVersion"), 0);

        const SettingsStore fresh;
        QVERIFY(!fresh.contains(QStringLiteral("model/recipeId")));
        QCOMPARE(fresh.modelRecipeId(), QString::fromUtf8(SettingsStore::kDefaultModelRecipeId));
        QSettings after;
        QCOMPARE(after.value(QStringLiteral("settings/schemaVersion")).toInt(), 1);
    }

    // The runtime paths point at the user's disk, not at the product's schema:
    // wiping them would make the app forget its installed runtime and models.
    void theResetKeepsTheRuntimePaths()
    {
        QSettings pre;
        pre.setValue(QStringLiteral("runtime/rootDir"), QStringLiteral("/tmp/llocr-runtime"));
        pre.setValue(QStringLiteral("runtime/modelsDir"), QStringLiteral("/tmp/llocr-models"));
        pre.setValue(QStringLiteral("settings/schemaVersion"), 0);

        const SettingsStore fresh;
        QCOMPARE(fresh.runtimeRootDir(), QStringLiteral("/tmp/llocr-runtime"));
        QCOMPARE(fresh.runtimeModelsDir(), QStringLiteral("/tmp/llocr-models"));
    }
};

QTEST_MAIN(TestSettingsStore)
#include "test_settings_store.moc"
