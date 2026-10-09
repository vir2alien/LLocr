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
    }

    void defaultValues()
    {
        SettingsStore store;
        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
        QCOMPARE(store.apiKey(), QStringLiteral(""));
        QCOMPARE(store.connectionTimeoutMs(), 120000);
        QCOMPARE(store.modelName(), QStringLiteral("Unlimited-OCR"));
        QCOMPARE(store.checkModelName(), QString());
        QCOMPARE(store.decisionModelName(), QString());
        // "auto" — the OCR model adapter declares its parser (ADR 88).
        QCOMPARE(store.parserId(), QStringLiteral("auto"));
        QCOMPARE(store.checkModelName(), QString());
        QCOMPARE(store.autoCheck(), false);
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
        store.setConnectionTimeoutMs(5000);
        store.setModelName(QStringLiteral("custom-model"));
        store.setCheckModelName(QStringLiteral("check-model"));
        store.setParserId(QStringLiteral("raw"));
        store.setThemeMode(1);
        store.setLanguage(QStringLiteral("ru"));

        QCOMPARE(store.baseUrl(), QStringLiteral("http://custom:1234"));
        QCOMPARE(store.apiKey(), QStringLiteral("secret-token"));
        QCOMPARE(store.connectionTimeoutMs(), 5000);
        QCOMPARE(store.modelName(), QStringLiteral("custom-model"));
        QCOMPARE(store.parserId(), QStringLiteral("raw"));
        QCOMPARE(store.themeMode(), 1);
        QCOMPARE(store.language(), QStringLiteral("ru"));

        store.resetToDefaults();

        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
        QCOMPARE(store.apiKey(), QStringLiteral(""));
        QCOMPARE(store.connectionTimeoutMs(), 120000);
        QCOMPARE(store.modelName(), QStringLiteral("Unlimited-OCR"));
        QCOMPARE(store.checkModelName(), QString());
        // "auto" — the OCR model adapter declares its parser (ADR 88).
        QCOMPARE(store.parserId(), QStringLiteral("auto"));
        QCOMPARE(store.checkModelName(), QString());
        QCOMPARE(store.autoCheck(), false);
        QCOMPARE(store.themeMode(), 0);
        QCOMPARE(store.language(), QStringLiteral("system"));
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
        store.setConnectionTimeoutMs(5000);
        store.setModelName(QStringLiteral("ocr-model"));
        store.setCheckModelName(QStringLiteral("check-model"));
        store.setServerPath(QStringLiteral("/opt/llama-server"));
        store.setThemeMode(1);

        store.resetRuntimeDefaults();

        QCOMPARE(store.connectionMode(), QStringLiteral("external"));
        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
        QCOMPARE(store.apiKey(), QString());
        QCOMPARE(store.connectionTimeoutMs(), 120000);
        QCOMPARE(store.modelName(), QStringLiteral("Unlimited-OCR"));
        QCOMPARE(store.checkModelName(), QString());
        QCOMPARE(store.serverPath(), QString());
        // Outside the group — untouched.
        QCOMPARE(store.themeMode(), 1);
        QCOMPARE(store.language(), QStringLiteral("system"));
    }

    void startupMigrationRestoresWipedExternalEndpoint()
    {
        // Simulate a profile wiped by the wizard-step initialization bug:
        // external mode, empty endpoint, stashed last-external URL.
        {
            QSettings settings;
            settings.setValue("provider/mode", QStringLiteral("external"));
            settings.setValue("provider/baseUrl", QString());
            settings.setValue("provider/lastExternalBaseUrl", QStringLiteral("http://127.0.0.1:8080"));
            settings.setValue("runtime/setupVersion", 1);
        }

        SettingsStore store;
        QCOMPARE(store.baseUrl(), QStringLiteral("http://127.0.0.1:8080"));
    }

    void startupMigrationLeavesNonEmptyEndpointAlone()
    {
        {
            QSettings settings;
            settings.setValue("provider/mode", QStringLiteral("external"));
            settings.setValue("provider/baseUrl", QStringLiteral("http://mine:9000"));
            settings.setValue("provider/lastExternalBaseUrl", QStringLiteral("http://127.0.0.1:8080"));
            settings.setValue("runtime/setupVersion", 1);
        }

        SettingsStore store;
        QCOMPARE(store.baseUrl(), QStringLiteral("http://mine:9000"));
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
        QCOMPARE(store.launchProfileId(), QStringLiteral(""));
        QCOMPARE(store.autoCheck(), false);
        QCOMPARE(store.hfToken(), QStringLiteral(""));
    }

    // The check role's request profile is the id of its model profile. A
    // profile written before the split has nothing stored, and the verifier
    // would resolve to no sampling parameters at all.
    void checkRoleResolvesAModelProfileWithoutOneStored()
    {
        QSettings pre;
        pre.remove(QStringLiteral("check/requestProfileId"));

        const SettingsStore store;
        const QStringList checkModels = ModelProfiles::idsForRole(ModelProfiles::instance(), QStringLiteral("blockRecognition"));
        QVERIFY(!checkModels.isEmpty());
        QCOMPARE(store.checkRequestProfileId(), checkModels.constFirst());
        // Nothing is written: the default is a fallback, not a stored choice.
        QVERIFY(!store.contains(QStringLiteral("check/requestProfileId")));

        // An explicit choice still wins.
        SettingsStore chosen;
        chosen.setCheckRequestProfileId(QStringLiteral("qwen3.5-4b"));
        QCOMPARE(chosen.checkRequestProfileId(), QStringLiteral("qwen3.5-4b"));
    }

    // The key's default used to be the empty string, so every existing profile
    // holds one. QSettings::value() would return that empty string instead of
    // the fallback, and the verifier would end up with no sampling parameters
    // while the UI reported the model as unknown.
    void anEmptyStoredCheckProfileIsNotAChoice()
    {
        QSettings pre;
        pre.setValue(QStringLiteral("check/requestProfileId"), QString());

        const SettingsStore store;
        const QStringList checkModels = ModelProfiles::idsForRole(ModelProfiles::instance(), QStringLiteral("blockRecognition"));
        QVERIFY(!checkModels.isEmpty());
        QCOMPARE(store.checkRequestProfileId(), checkModels.constFirst());
    }

    // Same fallback rule as the check role: the decision profile id is the id
    // of its model profile, and nothing is stored until the user picks one.
    void decisionRoleResolvesAModelProfileWithoutOneStored()
    {
        QSettings pre;
        pre.remove(QStringLiteral("decision/requestProfileId"));

        const SettingsStore store;
        const QStringList decisionModels = ModelProfiles::idsForRole(ModelProfiles::instance(), QStringLiteral("decision"));
        QVERIFY(!decisionModels.isEmpty());
        QCOMPARE(store.decisionRequestProfileId(), decisionModels.constFirst());
        QVERIFY(!store.contains(QStringLiteral("decision/requestProfileId")));

        SettingsStore chosen;
        chosen.setDecisionRequestProfileId(decisionModels.constFirst());
        QCOMPARE(chosen.decisionRequestProfileId(), decisionModels.constFirst());
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

    // The parser family split into one parser per model, so the id that named
    // the shared parser is retired. A stored one is resolved to the parser the
    // selected model declares — after the model id itself is normalized, so a
    // model the catalog does not know follows the model it became.
    void aStoredRetiredParserIdIsResolved()
    {
        {
            QSettings pre;
            pre.setValue(QStringLiteral("parser/id"), QStringLiteral("det_tokens"));
            pre.setValue(QStringLiteral("model/recipeId"), QStringLiteral("lfm25-vl-3b"));

            const SettingsStore store;
            QCOMPARE(store.parserId(), QStringLiteral("lfm2.5-vl"));
        }
        {
            QSettings pre;
            pre.setValue(QStringLiteral("parser/id"), QStringLiteral("det_tokens"));
            pre.remove(QStringLiteral("model/recipeId"));

            const SettingsStore store;
            QCOMPARE(store.parserId(), QStringLiteral("unlimited-ocr"));
        }
        {
            // A stored parser id nobody knows is left alone: only the retired
            // one is rewritten, and an unknown id degrades to raw at use.
            QSettings pre;
            pre.setValue(QStringLiteral("parser/id"), QStringLiteral("no-such-parser"));

            const SettingsStore store;
            QCOMPARE(store.parserId(), QStringLiteral("no-such-parser"));
        }
    }

    // The alias is the model's, per role: one stored alias could only ever name
    // one of the two servers. The flags lost their reader when the path pickers
    // went away, and the request-profile id lost its reader when the model id
    // became the only selector; a stored value nobody reads is not harmless
    // either — it is a leftover the next reader would trust.
    void migrationDropsTheRetiredKeys()
    {
        const QStringList retired = {
            QStringLiteral("launch/modelAlias"),
            QStringLiteral("runtime/serverPathIsManaged"),
            QStringLiteral("launch/sourceDownload"),
            QStringLiteral("check/sourceDownload"),
            QStringLiteral("model/requestProfileId"),
        };

        QSettings pre;
        pre.setValue(QStringLiteral("launch/modelAlias"), QStringLiteral("stale-alias"));
        pre.setValue(QStringLiteral("runtime/serverPathIsManaged"), true);
        pre.setValue(QStringLiteral("launch/sourceDownload"), true);
        pre.setValue(QStringLiteral("check/sourceDownload"), true);
        pre.setValue(QStringLiteral("model/requestProfileId"), QStringLiteral("unlimited-ocr"));

        const SettingsStore store;
        for (const QString &key : retired)
            QVERIFY2(!store.contains(key), qPrintable(key));
    }

    // An id from the retired request-profile space ("ocr-verifier") is not a
    // choice: the catalog has no such model, so the app put the check model on
    // the generic launch fallback and showed a "not in the catalog" notice for a
    // model that is in the catalog. The migration resolves it and writes the
    // model profile back, so the next run reads a value it can act on.
    void aModelIdFromTheRetiredIdSpaceIsResolved()
    {
        QSettings pre;
        pre.setValue(QStringLiteral("check/requestProfileId"), QStringLiteral("ocr-verifier"));
        pre.setValue(QStringLiteral("model/recipeId"), QStringLiteral("ocr-verifier"));

        const SettingsStore store;
        const QStringList checkModels = ModelProfiles::idsForRole(ModelProfiles::instance(), QStringLiteral("blockRecognition"));
        const QString ocrModel = ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("ocr"));
        QVERIFY(!checkModels.isEmpty());
        QVERIFY(!ocrModel.isEmpty());
        QCOMPARE(store.checkRequestProfileId(), checkModels.constFirst());
        QCOMPARE(store.modelRecipeId(), ocrModel);
        // Written back, not just answered differently on every read.
        QCOMPARE(pre.value(QStringLiteral("check/requestProfileId")).toString(), checkModels.constFirst());
        QCOMPARE(pre.value(QStringLiteral("model/recipeId")).toString(), ocrModel);

        // A real choice survives: the check role does not include every model
        // the ocr role has, so the two resolve to different ids and both are kept.
        const QString ocrChoice = QStringLiteral("lfm25-vl-3b");
        QString checkChoice;
        for (const QString &id : checkModels) {
            if (id != ocrChoice) {
                checkChoice = id;
                break;
            }
        }
        QVERIFY2(!checkChoice.isEmpty(), "no check model outside the ocr fixture id");
        QSettings other;
        other.setValue(QStringLiteral("check/requestProfileId"), checkChoice);
        other.setValue(QStringLiteral("model/recipeId"), ocrChoice);
        const SettingsStore kept;
        QCOMPARE(kept.checkRequestProfileId(), checkChoice);
        QCOMPARE(kept.modelRecipeId(), ocrChoice);
    }

    // Activating a model of a known family has to pick that family's profile:
    // the weights on disk would otherwise run with another model's launch
    // parameters. A repo the catalog does not know, or one that does not answer
    // the role, leaves the selection alone.
    void activatingAModelSelectsItsFamilyProfile()
    {
        SettingsStore store;
        store.setCheckRequestProfileId(QStringLiteral("qwen3.5-4b"));
        store.setModelRecipeId(QStringLiteral("unlimited-ocr"));

        store.selectModelProfile(QStringLiteral("LiquidAI/LFM2.5-VL-3B-GGUF"), QStringLiteral("ocr"));
        QCOMPARE(store.modelRecipeId(), QStringLiteral("lfm25-vl-3b"));

        // The same family also answers the check role — LFM2.5-VL is a
        // general-purpose VL model, so activation as the verifier follows its
        // own profile just the same.
        store.selectModelProfile(QStringLiteral("LiquidAI/LFM2.5-VL-3B-GGUF"), QStringLiteral("blockRecognition"));
        QCOMPARE(store.checkRequestProfileId(), QStringLiteral("lfm25-vl-3b"));

        store.selectModelProfile(QStringLiteral("unsloth/Qwen3.5-4B-MTP-GGUF"), QStringLiteral("blockRecognition"));
        QCOMPARE(store.checkRequestProfileId(), QStringLiteral("qwen3.5-4b"));

        // A family that does not answer the check role (OCR-only) leaves the
        // selection alone.
        store.selectModelProfile(QStringLiteral("sahilchachra/Unlimited-OCR-GGUF"), QStringLiteral("blockRecognition"));
        QCOMPARE(store.checkRequestProfileId(), QStringLiteral("qwen3.5-4b"));

        // A hand-picked GGUF joins to nothing.
        store.setModelRecipeId(QStringLiteral("unlimited-ocr"));
        store.selectModelProfile(QString(), QStringLiteral("ocr"));
        store.selectModelProfile(QStringLiteral("someone/Their-Model-GGUF"), QStringLiteral("ocr"));
        QCOMPARE(store.modelRecipeId(), QStringLiteral("unlimited-ocr"));
    }

    // qmllint cannot resolve Settings (it is registered by hand in main.cpp, not
    // a module type), so a handler for a signal that no longer exists is
    // invisible to the lint gate and surfaces only as a runtime warning. This
    // walks the QML, pairs each Connections block with its target, and asks the
    // meta-object of the targets this test can see.
    void qmlSignalHandlersExist()
    {
        const QDir root(QStringLiteral(LLOCR_SOURCE_DIR) + QStringLiteral("/resources/qml"));
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

    void autoCheckRoundTrip()
    {
        SettingsStore store;
        QCOMPARE(store.autoCheck(), false);
        store.setAutoCheck(true);
        QCOMPARE(store.autoCheck(), true);
        store.setAutoCheck(true);  // no-op, no signal spam
        QCOMPARE(store.autoCheck(), true);
        store.setAutoCheck(false);
        QCOMPARE(store.autoCheck(), false);
    }

    void migrationConfiguredProfileKeepsExternal()
    {
        // A pre-existing profile with a configured baseUrl must be treated as
        // already set up: no first-run wizard, and the mode stays External.
        QSettings pre;
        pre.setValue(QStringLiteral("provider/baseUrl"), QStringLiteral("http://localhost:8080"));
        pre.setValue(QStringLiteral("provider/apiKey"), QStringLiteral("k"));
        pre.sync();

        SettingsStore store;
        QCOMPARE(store.setupVersion(), SettingsStore::kCurrentSetupVersion);
        QCOMPARE(store.connectionMode(), QStringLiteral("external"));
        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
    }

    void migrationCleanProfileGivesZero()
    {
        // A fresh profile (no provider/baseUrl) must yield setupVersion == 0
        // so the first-run wizard shows up.
        SettingsStore store;
        QCOMPARE(store.setupVersion(), 0);
        QCOMPARE(store.connectionMode(), QStringLiteral("external"));
    }

    void migrationRunsOnlyOnce()
    {
        // After the first construction setupVersion exists, so re-construction
        // must not touch anything (e.g. must not flip an explicit Managed mode
        // back to External).
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
        //  - connectionMode / lastExternalBaseUrl: covered implicitly through
        //    setConnectionMode()'s restore path (baseUrl is reset, but its
        //    final value when coming from Managed is the restored endpoint).
        auto excluded = [](const QByteArray &name) { return name.startsWith("window") || name == QByteArrayLiteral("connectionMode") || name == QByteArrayLiteral("lastExternalBaseUrl"); };

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

    void resetEmitsChangedSignals()
    {
        // QML binds to the per-property NOTIFY signals; after a reset every
        // changed setting must announce itself (review 3.5 keeps this via the
        // setters, which emit on change).
        SettingsStore store;
        QSignalSpy languageSpy(&store, &SettingsStore::languageChanged);
        QSignalSpy baseUrlSpy(&store, &SettingsStore::baseUrlChanged);
        QSignalSpy autoStartSpy(&store, &SettingsStore::autoStartChanged);
        QSignalSpy launchHostSpy(&store, &SettingsStore::launchHostChanged);

        store.setLanguage(QStringLiteral("ru"));
        store.setBaseUrl(QStringLiteral("http://custom:1"));
        store.setAutoStart(true);
        store.setLaunchHost(QStringLiteral("10.0.0.1"));

        QCOMPARE(languageSpy.count(), 1);
        QCOMPARE(baseUrlSpy.count(), 1);
        QCOMPARE(autoStartSpy.count(), 1);
        QCOMPARE(launchHostSpy.count(), 1);

        store.resetToDefaults();

        QCOMPARE(languageSpy.count(), 2);  // set + reset
        QCOMPARE(baseUrlSpy.count(), 2);
        QCOMPARE(autoStartSpy.count(), 2);
        QCOMPARE(launchHostSpy.count(), 2);
    }
};

QTEST_MAIN(TestSettingsStore)
#include "test_settings_store.moc"
