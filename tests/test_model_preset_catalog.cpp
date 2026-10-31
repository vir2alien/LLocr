#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "core/DecisionRequest.h"
#include "core/ModelProfiles.h"
#include "runtime/ModelCatalog.h"
#include "runtime/ModelPreset.h"
#include "runtime/ModelPresetCatalog.h"

using namespace llocr;

// Covers §4.6 (ModelPresetCatalog was untested) and the §4.1 schema test:
// the shipped built-in catalog must always parse and carry the fields the
// install pipeline relies on.
class TestModelPresetCatalog : public QObject
{
    Q_OBJECT

private slots:
    void builtInCatalogParses();
    // A copy-paste between two profiles once gave the verifier role to an OCR
    // model, and the two offered each other's rows. The role split and the
    // parameter lists are what make a model recognisable, so both are checked
    // against the values the model card and the launcher actually document.
    void shippedProfilesAreTheOnesTheDocsDescribe();
    void mineruShipsTheTrainedTwoStepProtocol();
    void everyShippedQuantResolvesToItsName();
    void draftModuleReachesThePresets();
    void roleDefaultsFollowTheShippedProfiles();
    void verifierSystemPromptFollowsTheModel();
    void checkModelBlockPromptsOverridePerType();
    void fixRolesShipTheCorrectionProtocol();
    void fixRolesShipTheReasoningMode();
    void requestProfilesCarryTheSharedRequests();
    void runtimeNoteReachesTheSettings();
    void mergeByUserPrecedence();
    void importToJsonRoundTrips();
    void saveThenReset();
};

void TestModelPresetCatalog::builtInCatalogParses()
{
    // load() with an empty user path exercises just the built-in resource,
    // which must always be present and parse cleanly.
    QString err;
    const QList<ModelPreset> presets = ModelPresetCatalog::load(ModelPresetCatalog::expand(ModelProfiles::instance()), QString(), err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QVERIFY(presets.size() >= 2);  // the shipped presets

    for (const ModelPreset &p : presets) {
        // Schema contract: the fields the pipeline consumes are present.
        QVERIFY(!p.id.isEmpty());
        QVERIFY(!p.title.isEmpty());
        QVERIFY(!p.repo.isEmpty());
        QVERIFY(!p.model.isEmpty());  // at least a main model file
        QVERIFY(p.ctxSize > 0);
        // minBuild is optional: a model the catalog does not gate on a build
        // ships none.
        QVERIFY(!p.license.isEmpty());
        // Every entry names a model profile, and that profile answers at least
        // one role — otherwise the entry could never be offered in a window.
        QVERIFY(!p.profileId.isEmpty());
        const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), p.profileId);
        QVERIFY2(profile, qPrintable(QStringLiteral("preset %1 names unknown profile %2").arg(p.id, p.profileId)));
        QVERIFY2(!profile->roles.isEmpty(), qPrintable(QStringLiteral("preset %1 names profile %2 with no task").arg(p.id, p.profileId)));
    }

    // A model that serves one role is offered only there, and the catalog does
    // not carry the role itself: two lists could disagree with the profile. A
    // model may serve both — the same weights, two tasks — and the roles it
    // declares are pinned per model below.
}

void TestModelPresetCatalog::shippedProfilesAreTheOnesTheDocsDescribe()
{
    struct Expected {
        const char *id;
        const char *roles;  // comma-separated, as shipped
        const char *minBuild;
        const char *quants;  // comma-separated, as shipped
        const char *launch;
    };
    // The launch lists are the ones a wrong edit silently truncates: a model
    // that loses image-*-tokens or dry-sequence-breaker still starts, and the only
    // symptom is a worse vision budget; the 12B losing ubatch-size still starts
    // and aborts on the first image decode. ctx-size and n-predict are not listed on
    // purpose — they follow the machine's memory and live in the platform
    // profiles of serverLaunch.json (ADR 126).
    const QList<Expected> expected = {
        {"qwen3.5-4b", "fix", "b4000", "q8_0,q6_k,q5_k_m,q4_k_xl,iq4_xs", "spec-type,spec-draft-n-max,special,temp,parallel,no-warmup,jinja,reasoning-budget,flash-attn,image-min-tokens"},
        {"qwen3.5-9b", "fix", "b4000", "q8_0,q6_k,q5_k_m,q4_k_xl,iq4_xs", "spec-type,spec-draft-n-max,special,temp,parallel,no-warmup,jinja,reasoning-budget,flash-attn,image-min-tokens"},
        {"gemma-4-e4b", "fix", "b9600", "q4_k_xl,q2_k_xl", "spec-type,spec-draft-n-max,temp,parallel,no-warmup,jinja,reasoning-budget,flash-attn,image-min-tokens"},
        {"gemma-4-12b", "fix", "b9600", "q4_k_xl", "spec-type,spec-draft-n-max,temp,parallel,no-warmup,jinja,reasoning-budget,image-min-tokens,ubatch-size"},
        {"glm-ocr", "blockRecognition", "b8096", "q8_0,f16", "flash-attn,image-min-tokens,image-max-tokens"},
        {"mineru", "layout,blockRecognition", "b4000", "q8_0,q6_k,q5_k_m,q4_k_m,q3_k_m", "special"},
        {"minicpm-jev", "decision", "b11928", "q8_0,q6_k,q5_k_m,q4_k_m,iq4_xs", "ctx-size,batch-size,ubatch-size,flash-attn"},
    };

    for (const Expected &e : expected) {
        const QString id = QString::fromUtf8(e.id);
        const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), id);
        QVERIFY2(profile, qPrintable(id));
        QCOMPARE(profile->minBuild, QString::fromUtf8(e.minBuild));

        QStringList quants;
        for (const ModelProfiles::Quant &q : profile->files.quants)
            quants.append(q.id);
        QCOMPARE(quants.join(QLatin1Char(',')), QString::fromUtf8(e.quants));

        // Exactly the roles this model serves — a stray second role means a
        // profile was pasted from the wrong file. Compared as a set: the role
        // keys live in a QHash and have no order to compare against.
        const QStringList declared = QString::fromUtf8(e.roles).split(QLatin1Char(','));
        QCOMPARE(profile->roles.size(), declared.size());
        for (const QString &roleName : declared)
            QVERIFY2(ModelProfiles::roleFor(*profile, roleName), qPrintable(QStringLiteral("%1 does not declare the role %2").arg(id, roleName)));
        for (const QString &roleName : declared) {
            const ModelProfiles::Role *role = ModelProfiles::roleFor(*profile, roleName);
            QVERIFY2(role, qPrintable(id));
            QStringList launch;
            for (const LaunchParameter &p : role->launch)
                launch.append(p.name);
            QCOMPARE(launch.join(QLatin1Char(',')), QString::fromUtf8(e.launch));
        }

        // A model that serves both roles has to declare the same launch layer
        // in both: the flags are startup flags, so a difference makes the
        // runtime unload and reload the very model the user picked once.
        if (declared.size() == 2) {
            const QList<ModelProfiles::Role> served = profile->roles.values();
            QVERIFY2(served.at(0).launch == served.at(1).launch, qPrintable(QStringLiteral("%1: the two tasks declare different launch parameters, so switching task reloads the model").arg(id)));
        }
    }
}

// MinerU2.5-Pro trains one two-step document parse (the mineru-vl-utils
// protocol): layout detection over the 1036²-squashed page, then per-block
// transcription by type. No single-shot page mode exists in its prompt set.
void TestModelPresetCatalog::mineruShipsTheTrainedTwoStepProtocol()
{
    const ModelProfiles::Role *layout = ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("layout"));
    QVERIFY(layout);
    QCOMPARE(layout->parser, QStringLiteral("mineru-layout"));
    QCOMPARE(layout->prompts.size(), 1);
    QCOMPARE(layout->prompts.constFirst().text, QStringLiteral("\nLayout Detection:"));
    QCOMPARE(layout->systemPrompt, QStringLiteral("You are a helpful assistant."));
    // The model's own vocabulary styled in the profile; the default label map
    // already covers text/title/table/equation/image.
    QCOMPARE(layout->blockStyles.value(QStringLiteral("doc_title")), QStringLiteral("heading"));
    QCOMPARE(layout->blockStyles.value(QStringLiteral("list_item")), QStringLiteral("text"));
    QCOMPARE(layout->blockStyles.value(QStringLiteral("caption")), QStringLiteral("italic"));

    const ModelProfiles::Role *blocks = ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("blockRecognition"));
    QVERIFY(blocks);
    QCOMPARE(blocks->parser, QStringLiteral("raw"));
    QCOMPARE(blocks->blockPrompts.value(QStringLiteral("text")), QStringLiteral("\nText Recognition:"));
    QCOMPARE(blocks->blockPrompts.value(QStringLiteral("table")), QStringLiteral("\nTable Recognition:"));
    QCOMPARE(blocks->blockPrompts.value(QStringLiteral("equation")), QStringLiteral("\nFormula Recognition:"));

    // Every type the layout pass can name needs a block prompt, or the markup
    // pipeline sends an empty one.
    for (auto it = layout->blockStyles.constBegin(); it != layout->blockStyles.constEnd(); ++it) {
        if (it.value() == QLatin1String("image"))
            continue;
        QVERIFY2(blocks->blockPrompts.contains(it.key()), qPrintable(QStringLiteral("no block prompt for %1").arg(it.key())));
        QVERIFY2(!blocks->blockPrompts.value(it.key()).isEmpty(), qPrintable(QStringLiteral("empty block prompt for %1").arg(it.key())));
    }

    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("decision")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("fix")));
}

// Every shipped quantization must resolve to a name the UI can show. A file
// whose name carries a truncated quant token ("…-UD-Q4_K_XL.gguf") or a
// lowercase one (GLM-OCR-f16) is the case that used to leave the quant
// unnamed.
void TestModelPresetCatalog::everyShippedQuantResolvesToItsName()
{
    for (const ModelProfiles::Profile &profile : ModelProfiles::instance()) {
        for (const ModelProfiles::Quant &quant : profile.files.quants) {
            const QString resolved = ModelCatalog::quantizationForFile(profile.files.repo, quant.file);
            QVERIFY2(!resolved.isEmpty(), qPrintable(QStringLiteral("%1: quant %2 (%3) resolves to no name").arg(profile.id, quant.id, quant.file)));
            QCOMPARE(resolved, quant.id.toUpper());
        }
    }
}

// A check model carries its own wording per block type and its own system
// prompt — the profile is the only source of the verifier's wording.
// The Gemma 4 drafter ships inside the model's own repository (repo-root
// mtp-*.gguf), so the preset must resolve the draft's repo to the model's —
// a mtpRepo left empty is fine only because the install pipeline makes that
// substitution.
void TestModelPresetCatalog::draftModuleReachesThePresets()
{
    const QList<ModelPreset> presets = ModelPresetCatalog::expand(ModelProfiles::instance());

    const ModelPreset *gemma = nullptr;
    for (const ModelPreset &p : presets) {
        if (p.profileId == QStringLiteral("gemma-4-e4b")) {
            gemma = &p;
            break;
        }
    }
    QVERIFY2(gemma, "the gemma-4-e4b preset is in the built-in catalog");
    QCOMPARE(gemma->mtp, QStringLiteral("mtp-gemma-4-E4B-it.gguf"));
    QCOMPARE(gemma->mtpRepo, gemma->repo);
}

// The role's default model is a shipped-profile fact: the alphabetically first
// profile serving the role is only the fallback, so a new model whose id sorts
// early (gemma-4-12b) must not silently move the default the settings and the
// wizard fall back to.
void TestModelPresetCatalog::roleDefaultsFollowTheShippedProfiles()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    // The four tasks, each with its role-default model.
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("blockRecognition")), QStringLiteral("mineru"));
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("decision")), QStringLiteral("minicpm-jev"));
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("layout")), QStringLiteral("mineru"));
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("fix")), QStringLiteral("qwen3.5-4b"));

    // Exactly one default per task: the auto-pipeline picks "the default model
    // for the role" and must never face a tie or a gap that would silently
    // fall back to the first-in-list.
    const QList<QPair<const char *, int>> tasks = {{"blockRecognition", 1}, {"decision", 1}, {"layout", 1}, {"fix", 1}};
    for (const auto &task : tasks) {
        const QString role = QString::fromUtf8(task.first);
        int flagged = 0;
        for (const ModelProfiles::Profile &profile : profiles) {
            const ModelProfiles::Role *modelRole = ModelProfiles::roleFor(profile, role);
            if ((modelRole && modelRole->isDefault) || (profile.isDefault && modelRole))
                ++flagged;
        }
        QCOMPARE(flagged, task.second);
    }

    // The installer's picker sorts the role's recommended model first.
    QCOMPARE(ModelProfiles::forRole(profiles, QStringLiteral("blockRecognition")).constFirst().id, QStringLiteral("mineru"));
}

// Every check profile ships the verifier system prompt in the model's own
// idiom; a role without one — and an unknown model — resolves to nothing.
void TestModelPresetCatalog::verifierSystemPromptFollowsTheModel()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();

    // The block-recognition contract: transcribe the crop, reply with the
    // block text alone.
    const QString blocks = ModelProfiles::systemPromptFor(profiles, QStringLiteral("glm-ocr"), QStringLiteral("blockRecognition"));
    QVERIFY(blocks.contains(QStringLiteral("transcribe one block")));
    QVERIFY(blocks.contains(QStringLiteral("the block text and nothing else")));

    // The shipped decision is the visual diff (docs/31): the task wording is
    // baked into the model's own prompt, so the role ships no system prompt —
    // the request carries no texts to override.
    QVERIFY(ModelProfiles::systemPromptFor(profiles, QStringLiteral("minicpm-jev"), QStringLiteral("decision")).isEmpty());

    // An unknown model resolves to nothing.
    QVERIFY(ModelProfiles::systemPromptFor(profiles, QStringLiteral("unknown"), QStringLiteral("blockRecognition")).isEmpty());
}

// The fix task's profiles speak the same protocol (ADR 192): the system
// prompt pins the three-token contract (OK / FIX / REVIEW) and the language
// placeholder, the per-type prompts describe what a FIX may change, and the
// alias map keeps formula/reference out of the prompt table.
void TestModelPresetCatalog::fixRolesShipTheCorrectionProtocol()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    const QStringList fixModels = ModelProfiles::idsForRole(profiles, QStringLiteral("fix"));
    QCOMPARE(fixModels, QStringList({QStringLiteral("gemma-4-12b"), QStringLiteral("gemma-4-e4b"), QStringLiteral("qwen3.5-4b"), QStringLiteral("qwen3.5-9b")}));

    for (const QString &id : fixModels) {
        const ModelProfiles::Role *fix = ModelProfiles::roleFor(id, QStringLiteral("fix"));
        QVERIFY2(fix, qPrintable(id));
        QCOMPARE(fix->protocol, QStringLiteral("full_block"));
        const QString system = fix->systemPrompt;
        QVERIFY2(system.contains(QStringLiteral("exactly one of these")), qPrintable(id));
        QVERIFY2(system.contains(QStringLiteral("OK")), qPrintable(id));
        QVERIFY2(system.contains(QStringLiteral("FIX")), qPrintable(id));
        QVERIFY2(system.contains(QStringLiteral("REVIEW")), qPrintable(id));
        // EMPTY is gone from the scheme, and the language is a placeholder the
        // app fills in.
        QVERIFY2(!system.contains(QStringLiteral("EMPTY")), qPrintable(id));
        QVERIFY2(system.contains(QStringLiteral("{{LANGUAGE}}")), qPrintable(id));
        QVERIFY2(system.contains(QStringLiteral("Never truncate output")), qPrintable(id));
        // The reply carries the whole block and nothing else.
        QVERIFY2(system.contains(QStringLiteral("<the complete corrected block>")), qPrintable(id));
        QVERIFY2(fix->blockAliases.value(QStringLiteral("formula")) == QStringLiteral("equation"), qPrintable(id));
        QVERIFY2(fix->blockAliases.value(QStringLiteral("reference")) == QStringLiteral("ref_text"), qPrintable(id));
        QVERIFY2(!fix->blockMaxOutput.isEmpty(), qPrintable(id));
        QVERIFY2(!fix->blockPrompts.isEmpty(), qPrintable(id));
        for (auto it = fix->blockPrompts.constBegin(); it != fix->blockPrompts.constEnd(); ++it) {
            QVERIFY2(it.value().contains(QStringLiteral("Mode after FIX: the complete corrected")), qPrintable(QStringLiteral("%1: %2").arg(id, it.key())));
            // The span contract is gone: no prompt may ask for `old => new`
            // edits or for the old table's EDITS: section.
            QVERIFY2(!it.value().contains(QStringLiteral("old => new")), qPrintable(QStringLiteral("%1: %2").arg(id, it.key())));
            QVERIFY2(!it.value().contains(QStringLiteral("EDITS:")), qPrintable(QStringLiteral("%1: %2").arg(id, it.key())));
        }
    }

    // The per-block output budget resolves through the alias chain: a formula
    // asks for the equation budget, an unknown type falls back to -1 (the
    // request profile's max_tokens stays untouched). The reply carries the
    // whole block, so the budget must fit the block (ADR 192).
    QCOMPARE(ModelProfiles::maxOutputForBlock(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("formula")), 2048);
    QCOMPARE(ModelProfiles::maxOutputForBlock(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("reference")), 1024);
    QCOMPARE(ModelProfiles::maxOutputForBlock(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("image_caption")), 1024);
    QCOMPARE(ModelProfiles::maxOutputForBlock(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("page_number")), 64);
    QCOMPARE(ModelProfiles::maxOutputForBlock(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("unknown-type")), -1);

    // The alias hop serves the formula/reference prompts (the byte-for-byte
    // duplicates are gone from the table), and key_value — the one decision
    // type the fix vocabulary used to miss — ships its own.
    const ModelProfiles::Role *qwenFix = ModelProfiles::roleFor(QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"));
    QVERIFY(qwenFix);
    QVERIFY(!qwenFix->blockPrompts.contains(QStringLiteral("formula")));
    QVERIFY(!qwenFix->blockPrompts.contains(QStringLiteral("reference")));
    QVERIFY(!ModelProfiles::blockPromptFor(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("formula")).isEmpty());
    QVERIFY(!ModelProfiles::blockPromptFor(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("reference")).isEmpty());
    QVERIFY(!ModelProfiles::blockPromptFor(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("key_value")).isEmpty());

    // The equivalence rules are synced with the decision v6 block prompts
    // (docs/21): the fixer treats the same things as equivalent and strictly
    // different, or the two models would fight over е/ё, homoglyphs, LaTeX
    // commands and table cells. All four fix models ship identical prompts.
    const QString fixText = ModelProfiles::blockPromptFor(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("text"));
    QVERIFY(fixText.contains(QStringLiteral("Never fix:")));
    QVERIFY(fixText.contains(QStringLiteral("е/ё")));
    const QString fixCode = ModelProfiles::blockPromptFor(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("code"));
    QVERIFY(fixCode.contains(QStringLiteral("Nothing is equivalent here")));
    const QString fixEquation = ModelProfiles::blockPromptFor(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("equation"));
    QVERIFY(fixEquation.contains(QStringLiteral("the complete corrected formula as LaTeX")));
    QVERIFY(fixEquation.contains(QStringLiteral("Do not change spacing, aliases, or operand order")));
    const QString fixTable = ModelProfiles::blockPromptFor(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("table"));
    QVERIFY(fixTable.contains(QStringLiteral("the complete corrected table, then stop")));
}

void TestModelPresetCatalog::checkModelBlockPromptsOverridePerType()
{
    const ModelProfiles::Role *role = ModelProfiles::roleFor(QStringLiteral("glm-ocr"), QStringLiteral("blockRecognition"));
    QVERIFY(role);
    QVERIFY(!role->blockPrompts.isEmpty());

    const QStringList expected = {QStringLiteral("text"),
                                  QStringLiteral("title"),
                                  QStringLiteral("list"),
                                  QStringLiteral("table"),
                                  QStringLiteral("table_caption"),
                                  QStringLiteral("image_caption"),
                                  QStringLiteral("table_footnote"),
                                  QStringLiteral("figure_footnote"),
                                  QStringLiteral("equation"),
                                  QStringLiteral("formula"),
                                  QStringLiteral("code"),
                                  QStringLiteral("abstract"),
                                  QStringLiteral("ref_text"),
                                  QStringLiteral("reference"),
                                  QStringLiteral("header"),
                                  QStringLiteral("footer"),
                                  QStringLiteral("page_number"),
                                  QStringLiteral("seal")};
    for (const QString &type : expected)
        QVERIFY2(!role->blockPrompts.value(type).isEmpty(), qPrintable(type));

    // The per-type prompts are the trained prefixes: text kinds transcribe,
    // tables come out as a table, formulas as LaTeX.
    QCOMPARE(role->blockPrompts.value(QStringLiteral("text")), QStringLiteral("Text Recognition:"));
    QCOMPARE(role->blockPrompts.value(QStringLiteral("table")), QStringLiteral("Table Recognition:"));
    QCOMPARE(role->blockPrompts.value(QStringLiteral("equation")), QStringLiteral("Formula Recognition:"));

    // The fix task's table prompt pins the plain-markup contract for the
    // corrected table: this family wraps tables in ```html fences and
    // decorates the tags with border/style/data-* attributes unless told not
    // to, and neither survives into the block text.
    const QString qwenTable = ModelProfiles::blockPromptFor(ModelProfiles::instance(), QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"), QStringLiteral("table"));
    QVERIFY(qwenTable.contains(QStringLiteral("rowspan")));
    QVERIFY(qwenTable.contains(QStringLiteral("colspan")));

    // A type outside the catalog, and a model outside the catalog, resolve
    // to nothing.
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    QVERIFY(ModelProfiles::blockPromptFor(profiles, QStringLiteral("glm-ocr"), QStringLiteral("blockRecognition"), QStringLiteral("unknown-type")).isEmpty());
    QVERIFY(!ModelProfiles::blockPromptFor(profiles, QStringLiteral("glm-ocr"), QStringLiteral("blockRecognition"), QStringLiteral("formula")).isEmpty());
    QVERIFY(ModelProfiles::blockPromptFor(profiles, QStringLiteral("some-guf"), QStringLiteral("blockRecognition"), QStringLiteral("formula")).isEmpty());
}

// The reasoning mode is optional: the model profile says whether the model
// can think at all, the request profile offers the modes every fix model
// shares. A model without the flag gets no parameter and no thinking (ADR 193).
void TestModelPresetCatalog::fixRolesShipTheReasoningMode()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    const ModelProfiles::PromptProfile *requests = ModelProfiles::promptProfile(QStringLiteral("fix-requests"));
    QVERIFY(requests);
    QCOMPARE(requests->reasoningModes.size(), 4);
    QCOMPARE(requests->reasoningModes.at(0).label, QStringLiteral("off"));
    QCOMPARE(requests->reasoningModes.at(0).value.toDouble(), 0.0);
    QCOMPARE(requests->reasoningModes.at(1).label, QStringLiteral("low"));
    QCOMPARE(requests->reasoningModes.at(1).value.toDouble(), 512.0);
    QCOMPARE(requests->reasoningModes.at(2).label, QStringLiteral("med"));
    QCOMPARE(requests->reasoningModes.at(2).value.toDouble(), 1024.0);
    QCOMPARE(requests->reasoningModes.at(3).label, QStringLiteral("high"));
    QCOMPARE(requests->reasoningModes.at(3).value.toDouble(), 1536.0);

    for (const QString &id : ModelProfiles::idsForRole(profiles, QStringLiteral("fix"))) {
        const ModelProfiles::Role *fix = ModelProfiles::roleFor(id, QStringLiteral("fix"));
        QVERIFY2(fix, qPrintable(id));

        const RequestParameter *budget = nullptr;
        for (const RequestParameter &parameter : fix->request) {
            if (parameter.name == QLatin1String(kReasoningBudgetParameter))
                budget = &parameter;
        }
        if (!fix->reasoningSupported) {
            // A model that cannot think has no budget to set.
            QVERIFY2(budget == nullptr, qPrintable(id));
            continue;
        }
        QVERIFY2(budget, qPrintable(id));
        QCOMPARE(budget->value.toDouble(), 0.0);
        QCOMPARE(budget->kind, RequestValueKind::Number);
        QCOMPARE(budget->choices.size(), 4);
        QCOMPARE(budget->choices.at(1).label, QStringLiteral("low"));
        QCOMPARE(budget->choices.at(1).value.toDouble(), 512.0);
    }

    // Every fix model may think: the qwen pair and the gemma pair both declare
    // the capability (the gemma chat template carries the enable_thinking switch
    // and the thought channel — verified on llama.cpp b11799).
    QVERIFY(ModelProfiles::roleFor(QStringLiteral("qwen3.5-4b"), QStringLiteral("fix"))->reasoningSupported);
    QVERIFY(ModelProfiles::roleFor(QStringLiteral("qwen3.5-9b"), QStringLiteral("fix"))->reasoningSupported);
    QVERIFY(ModelProfiles::roleFor(QStringLiteral("gemma-4-e4b"), QStringLiteral("fix"))->reasoningSupported);
    QVERIFY(ModelProfiles::roleFor(QStringLiteral("gemma-4-12b"), QStringLiteral("fix"))->reasoningSupported);
}

// The fix task sends the same request whatever model serves it, so the request
// texts live in their own profile and the model roles only name it (ADR 182).
// The parameters — launch, request, thresholds — stay in the model profile.
void TestModelPresetCatalog::requestProfilesCarryTheSharedRequests()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();

    // One request profile, the fix task's; the decision speaks the visual
    // diff (docs/31) and carries no texts at all.
    QCOMPARE(ModelProfiles::promptProfiles().size(), 1);
    QVERIFY(ModelProfiles::promptProfile(QStringLiteral("fix-requests")));
    QVERIFY(!ModelProfiles::promptProfile(QStringLiteral("decision-requests")));
    QVERIFY(!ModelProfiles::promptProfile(QStringLiteral("no-such-profile")));

    // Every fix model names the shared fix request profile and inherits its
    // texts; the parameters stay in the model profile.
    const ModelProfiles::PromptProfile *fixRequests = ModelProfiles::promptProfile(QStringLiteral("fix-requests"));
    QVERIFY(fixRequests);
    for (const QString &id : ModelProfiles::idsForRole(profiles, QStringLiteral("fix"))) {
        const ModelProfiles::Role *role = ModelProfiles::roleFor(id, QStringLiteral("fix"));
        QVERIFY2(role, qPrintable(id));
        QCOMPARE(role->promptProfile, QStringLiteral("fix-requests"));
        QCOMPARE(role->systemPrompt, fixRequests->prompts.systemPrompt);
        QVERIFY(!role->systemPrompt.isEmpty());
        QVERIFY(!role->blockPrompts.isEmpty());
        QVERIFY(!role->blockMaxOutput.isEmpty());
    }

    // The reply budgets are the protocol's own rule — the whole block travels
    // in the reply — so they live in the request profile, once, and every fix
    // model inherits them (ADR 192). A model profile that ships its own entry
    // keeps it; the four shipped fix models ship none.
    QVERIFY(!fixRequests->prompts.blockMaxOutput.isEmpty());
    QCOMPARE(fixRequests->prompts.blockMaxOutput.value(QStringLiteral("text")), 2048);
    QCOMPARE(fixRequests->prompts.blockMaxOutput.value(QStringLiteral("page_number")), 64);
    QCOMPARE(fixRequests->prompts.blockMaxOutput.value(QStringLiteral("table")), 8192);
    for (const QString &id : ModelProfiles::idsForRole(profiles, QStringLiteral("fix"))) {
        const ModelProfiles::Role *role = ModelProfiles::roleFor(id, QStringLiteral("fix"));
        QVERIFY2(role, qPrintable(id));
        QCOMPARE(role->blockMaxOutput, fixRequests->prompts.blockMaxOutput);
    }

    // The decision model speaks the visual diff (docs/31): the task wording
    // is baked into the model's own prompt, the server rejects a request that
    // sends texts, so no request profile exists and the role names none. The
    // thresholds stay per model, measured per block type (docs/31, ADR 184);
    // the flat 0.5 stands behind the split for a type the measurement did
    // not cover.
    for (const QString &id : ModelProfiles::idsForRole(profiles, QStringLiteral("decision"))) {
        const ModelProfiles::Role *role = ModelProfiles::roleFor(id, QStringLiteral("decision"));
        QVERIFY2(role, qPrintable(id));
        QCOMPARE(role->protocol, visualDiffDecisionProtocol());
        QVERIFY2(role->promptProfile.isEmpty(), qPrintable(id));
        QVERIFY(role->matchThreshold > 0);
    }
    const ModelProfiles::Role *jev = ModelProfiles::roleFor(QStringLiteral("minicpm-jev"), QStringLiteral("decision"));
    QVERIFY(jev);
    QCOMPARE(jev->matchThreshold, 0.5);
    QCOMPARE(jev->blockMatchThresholds.size(), 3);
    QCOMPARE(jev->blockMatchThresholds.value(QStringLiteral("text")), 0.92);
    QCOMPARE(jev->blockMatchThresholds.value(QStringLiteral("equation")), 0.87);
    QCOMPARE(jev->blockMatchThresholds.value(QStringLiteral("table")), 0.4);

    // The OCR models keep their own prompts inline — only the two shared tasks
    // moved.
    const ModelProfiles::Role *mineru = ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("layout"));
    QVERIFY(mineru);
    QVERIFY(mineru->promptProfile.isEmpty());
    QVERIFY(!mineru->systemPrompt.isEmpty());
}

// A model the managed runtime cannot load says so in its profile, and the note
// reaches the store the settings windows read. Fed from a profile built here, not
// from a shipped one: no shipped model needs a note, and a test that borrowed one
// would start failing the day its model became runnable.
void TestModelPresetCatalog::runtimeNoteReachesTheSettings()
{
    ModelProfiles::Profile unrunnable;
    unrunnable.id = QStringLiteral("needs-a-patch");
    unrunnable.title = unrunnable.id;
    unrunnable.runtimeNote = QStringLiteral("stock llama.cpp cannot load this GGUF");

    QList<ModelProfiles::Profile> profiles = {unrunnable, ModelProfiles::instance().constFirst()};
    QCOMPARE(ModelProfiles::runtimeNoteFor(profiles, QStringLiteral("needs-a-patch")), QStringLiteral("stock llama.cpp cannot load this GGUF"));
    // No note, no warning: every model the app ships runs on the runtime it
    // downloads.
    QVERIFY(ModelProfiles::runtimeNoteFor(profiles, profiles.last().id).isEmpty());
    QVERIFY(ModelProfiles::runtimeNoteFor(profiles, QStringLiteral("some-guf")).isEmpty());
}

void TestModelPresetCatalog::mergeByUserPrecedence()
{
    // Build a user catalog that overrides one built-in id and adds a new one.
    QList<ModelPreset> user;
    ModelPreset override_;
    override_.id = QStringLiteral("mineru-q4km");
    override_.title = QStringLiteral("User-tuned MinerU");
    override_.repo = QStringLiteral("user/override-repo");
    override_.model = QStringLiteral("model.gguf");
    user << override_;

    ModelPreset added;
    added.id = QStringLiteral("brand-new-preset");
    added.title = QStringLiteral("New");
    added.repo = QStringLiteral("user/new-repo");
    added.model = QStringLiteral("new.gguf");
    user << added;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userPath = QDir(dir.path()).filePath(QStringLiteral("catalog.json"));
    QString err;
    QVERIFY(ModelPresetCatalog::save(userPath, user, err));
    QVERIFY(err.isEmpty());

    const QList<ModelPreset> merged = ModelPresetCatalog::load(ModelPresetCatalog::expand(ModelProfiles::instance()), userPath, err);
    QVERIFY(err.isEmpty());

    // The overridden id now carries the user's title/repo.
    bool sawOverride = false;
    bool sawNew = false;
    for (const ModelPreset &p : merged) {
        if (p.id == override_.id) {
            sawOverride = true;
            QCOMPARE(p.title, QStringLiteral("User-tuned MinerU"));
            QCOMPARE(p.repo, QStringLiteral("user/override-repo"));
        }
        if (p.id == added.id)
            sawNew = true;
    }
    QVERIFY(sawOverride);
    QVERIFY(sawNew);
}

void TestModelPresetCatalog::importToJsonRoundTrips()
{
    ModelPreset p;
    p.id = QStringLiteral("x/y");
    p.title = QStringLiteral("T");
    p.repo = QStringLiteral("x/y");
    p.revision = QStringLiteral("deadbeef");
    p.model = QStringLiteral("model-Q4_K_M.gguf");
    p.mmproj = QStringLiteral("mmproj.gguf");
    p.ctxSize = 4096;
    p.minBuild = QStringLiteral("b4000");
    p.license = QStringLiteral("apache-2.0");
    p.sha256.insert(QStringLiteral("model-q4_k_m.gguf"), QStringLiteral("abcdef"));

    const ModelPreset restored = ModelPreset::fromJson(p.toJson());
    QCOMPARE(restored.id, p.id);
    QCOMPARE(restored.revision, p.revision);
    QCOMPARE(restored.model, p.model);
    QCOMPARE(restored.mmproj, p.mmproj);
    QCOMPARE(restored.ctxSize, 4096);
    QCOMPARE(restored.sha256.value(QStringLiteral("model-q4_k_m.gguf")), QStringLiteral("abcdef"));
    // Serialize→parse→serialize is stable.
    QCOMPARE(ModelPreset::fromJson(restored.toJson()).toJson().toVariantMap(), p.toJson().toVariantMap());
}

void TestModelPresetCatalog::saveThenReset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userPath = QDir(dir.path()).filePath(QStringLiteral("catalog.json"));

    QList<ModelPreset> presets;
    ModelPreset p;
    p.id = QStringLiteral("one");
    p.repo = QStringLiteral("org/one");
    p.model = QStringLiteral("one.gguf");
    presets << p;

    QString err;
    QVERIFY(ModelPresetCatalog::save(userPath, presets, err));
    QVERIFY(QFile::exists(userPath));

    // The file round-trips: load() reads the two sources and returns `one`.
    const QList<ModelPreset> loaded = ModelPresetCatalog::load(ModelPresetCatalog::expand(ModelProfiles::instance()), userPath, err);
    QVERIFY(err.isEmpty());
    bool sawSaved = false;
    for (const ModelPreset &pp : loaded)
        if (pp.id == QStringLiteral("one"))
            sawSaved = true;
    QVERIFY(sawSaved);

    // "Restore defaults" removes the user catalog.
    QVERIFY(ModelPresetCatalog::resetUserCatalog(userPath, err));
    QVERIFY(!QFile::exists(userPath));
    err.clear();
    // After reset the user catalog is gone: load() only yields built-ins.
    const QList<ModelPreset> afterReset = ModelPresetCatalog::load(ModelPresetCatalog::expand(ModelProfiles::instance()), userPath, err);
    QVERIFY(err.isEmpty());
    bool hasOne = false;
    for (const ModelPreset &pp : afterReset)
        if (pp.id == QStringLiteral("one"))
            hasOne = true;
    QVERIFY(!hasOne);
}

QTEST_MAIN(TestModelPresetCatalog)
#include "test_model_preset_catalog.moc"