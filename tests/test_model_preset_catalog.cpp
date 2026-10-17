#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "core/ModelProfiles.h"
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
    void lightonOcrShipsOnlyTheTrainedModes();
    void mineruShipsTheTrainedTwoStepProtocol();
    void paddleOcrVlShipsElementRecognitionOnly();
    void draftModuleReachesThePresets();
    void roleDefaultsFollowTheShippedProfiles();
    void verifierSystemPromptFollowsTheModel();
    void layoutRoleShipsTheMarkupContract();
    void teleOcrBlockPromptsCoverTheLayoutVocabulary();
    void checkModelBlockPromptsOverridePerType();
    void fixRolesShipTheCorrectionProtocol();
    void decisionRoleShipsTheStructuredStateWording();
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
    // symptom is a worse vision budget. ctx-size and n-predict are not listed on
    // purpose — they follow the machine's memory and live in the platform
    // profiles of serverLaunch.json (ADR 126).
    const QList<Expected> expected = {
        {"unlimited-ocr", "pageOcr", "b4000", "q8_0,q6_k,q5_k_m,q4_k_m,iq4_xs", "image-min-tokens,image-max-tokens,dry-sequence-breaker,special"},
        {"lfm25-vl-3b", "pageOcr,blockRecognition", "b8000", "q8_0,q6_k,q5_k_m,q4_k_m,q4_0", "special,spec-type,spec-draft-n-max,spec-draft-n-min"},
        {"qwen3.5-4b", "fix", "b4000", "q8_0,q6_k,q5_k_m,q4_k_xl,iq4_xs", "spec-type,spec-draft-n-max,special,temp,parallel,no-warmup,jinja,reasoning-budget,flash-attn,image-min-tokens"},
        {"qwen3.5-9b", "fix", "b4000", "q8_0,q6_k,q5_k_m,q4_k_xl,iq4_xs", "spec-type,spec-draft-n-max,special,temp,parallel,no-warmup,jinja,reasoning-budget,flash-attn,image-min-tokens"},
        {"gemma-4-e4b", "fix", "b9600", "q4_k_xl,q2_k_xl", "spec-type,spec-draft-n-max,temp,parallel,no-warmup,jinja,flash-attn,image-min-tokens"},
        {"gemma-4-12b", "fix", "b9600", "q4_k_xl", "spec-type,spec-draft-n-max,temp,parallel,no-warmup,jinja,image-min-tokens"},
        {"teleocr", "blockRecognition,layout", "b4000", "q8_0,q5_k_m,q4_k_m,q3_k_m", ""},
        {"glm-ocr", "rawPageOcr,blockRecognition", "b8096", "q8_0,f16", "flash-attn,image-min-tokens,image-max-tokens"},
        {"clef-flash", "decision", "b11430", "q8_0,q6_k,q5_k_m,q4_k_m,q4_0", "ctx-size,batch-size,ubatch-size"},
        {"d3-flash", "decision", "b11572", "q8_0,q4_k_m", "ctx-size,batch-size,ubatch-size"},
        {"d3-mini", "decision", "b11572", "q8_0,q4_k_m", "ctx-size,batch-size,ubatch-size"},
        {"lightonocr", "pageOcr,rawPageOcr,blockRecognition", "b4000", "q8_0,q6_k,q5_k_m,q4_k_m,q3_k_m", "flash-attn,jinja,reasoning-budget,image-min-tokens"},
        {"mineru", "layout,blockRecognition", "b4000", "q8_0,q6_k,q5_k_m,q4_k_m,q3_k_m", "special"},
        {"paddleocr-vl", "blockRecognition", "b8200", "f16", ""},
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

// LightOnOCR-3 trains exactly two prompts — empty for transcription,
// "grounding" for the labeled boxes — and the card calls every other
// instruction out of distribution. The profile must keep that split: page OCR
// grounds, page and block transcription run on the empty prompt (the block is
// just a smaller image), and no task outside the two prompts is claimed.
void TestModelPresetCatalog::lightonOcrShipsOnlyTheTrainedModes()
{
    const ModelProfiles::Role *pageOcr = ModelProfiles::roleFor(QStringLiteral("lightonocr"), QStringLiteral("pageOcr"));
    QVERIFY(pageOcr);
    QCOMPARE(pageOcr->parser, QStringLiteral("lightonocr"));
    QCOMPARE(pageOcr->prompts.size(), 1);
    QCOMPARE(pageOcr->prompts.constFirst().text, QStringLiteral("grounding"));
    // The card's label vocabulary, styled in the profile: the model writes its
    // own markdown (headings stay untouched), chart blocks carry their
    // extracted data table, captions and footnotes render italic.
    QCOMPARE(pageOcr->blockStyles.value(QStringLiteral("title")), QStringLiteral("text"));
    QCOMPARE(pageOcr->blockStyles.value(QStringLiteral("chart")), QStringLiteral("table"));
    QCOMPARE(pageOcr->blockStyles.value(QStringLiteral("formula")), QStringLiteral("equation"));
    QCOMPARE(pageOcr->blockStyles.value(QStringLiteral("caption")), QStringLiteral("italic"));
    QCOMPARE(pageOcr->blockStyles.value(QStringLiteral("header_image")), QStringLiteral("image"));

    // The transcription mode is the empty prompt: no prompts entry at all, the
    // reply is read raw.
    const ModelProfiles::Role *rawPageOcr = ModelProfiles::roleFor(QStringLiteral("lightonocr"), QStringLiteral("rawPageOcr"));
    QVERIFY(rawPageOcr);
    QCOMPARE(rawPageOcr->parser, QStringLiteral("raw"));
    QVERIFY(rawPageOcr->prompts.isEmpty());

    // Block recognition is the same empty prompt over a smaller image: no
    // system message, no per-type prefixes, the reply read raw.
    const ModelProfiles::Role *blocks = ModelProfiles::roleFor(QStringLiteral("lightonocr"), QStringLiteral("blockRecognition"));
    QVERIFY(blocks);
    QCOMPARE(blocks->parser, QStringLiteral("raw"));
    QVERIFY(blocks->prompts.isEmpty());
    QVERIFY(blocks->systemPrompt.isEmpty());
    QVERIFY(blocks->blockPrompts.isEmpty());

    // The card: "Other instructions are out of distribution."
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("lightonocr"), QStringLiteral("layout")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("lightonocr"), QStringLiteral("decision")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("lightonocr"), QStringLiteral("fix")));
}

// MinerU2.5-Pro trains one two-step document parse (the mineru-vl-utils
// protocol): layout detection over the 1036²-squashed page, then per-block
// transcription by type. No single-shot page mode exists in its prompt set,
// so pageOcr and rawPageOcr must stay unclaimed.
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

    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("pageOcr")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("rawPageOcr")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("decision")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("fix")));
}

// PaddleOCR-VL-1.6's VLM answers six element prompts (OCR, table, formula,
// chart, seal, spotting); the page layout is a separate RT-DETR model that
// ships outside the GGUF, so only the block task is claimed. The card's
// server invocation is --temp 0 with no system message.
void TestModelPresetCatalog::paddleOcrVlShipsElementRecognitionOnly()
{
    const ModelProfiles::Role *blocks = ModelProfiles::roleFor(QStringLiteral("paddleocr-vl"), QStringLiteral("blockRecognition"));
    QVERIFY(blocks);
    QCOMPARE(blocks->parser, QStringLiteral("raw"));
    QVERIFY(blocks->systemPrompt.isEmpty());
    QCOMPARE(blocks->blockPrompts.value(QStringLiteral("text")), QStringLiteral("OCR:"));
    QCOMPARE(blocks->blockPrompts.value(QStringLiteral("table")), QStringLiteral("Table Recognition:"));
    QCOMPARE(blocks->blockPrompts.value(QStringLiteral("equation")), QStringLiteral("Formula Recognition:"));
    QCOMPARE(blocks->blockPrompts.value(QStringLiteral("seal")), QStringLiteral("Seal Recognition:"));

    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("paddleocr-vl"), QStringLiteral("pageOcr")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("paddleocr-vl"), QStringLiteral("rawPageOcr")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("paddleocr-vl"), QStringLiteral("layout")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("paddleocr-vl"), QStringLiteral("decision")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("paddleocr-vl"), QStringLiteral("fix")));

    // The quant ladder lives with mradermacher, but the mmproj only ships in
    // the official repo — and a preset downloads everything from one repo,
    // so the profile pins the official one for both.
    const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), QStringLiteral("paddleocr-vl"));
    QVERIFY(profile);
    QCOMPARE(profile->files.repo, QStringLiteral("PaddlePaddle/PaddleOCR-VL-1.6-GGUF"));
    QVERIFY(profile->files.mmproj.repo.isEmpty());
}

// A check model carries its own wording per block type and its own system
// prompt — the profile is the only source of the verifier's wording.
// The DSpark draft ships in its own repo, so the preset has to carry both the
// file name and the repo — otherwise the install downloads nothing to run
// --model-draft with.
void TestModelPresetCatalog::draftModuleReachesThePresets()
{
    const QList<ModelPreset> presets = ModelPresetCatalog::expand(ModelProfiles::instance());
    const ModelPreset *lfm = nullptr;
    for (const ModelPreset &p : presets) {
        if (p.profileId == QStringLiteral("lfm25-vl-3b")) {
            lfm = &p;
            break;
        }
    }
    QVERIFY2(lfm, "the lfm25-vl-3b preset is in the built-in catalog");
    QCOMPARE(lfm->mtp, QStringLiteral("LFM2.5-VL-3B-DSpark-F16.gguf"));
    QCOMPARE(lfm->mtpRepo, QStringLiteral("LiquidAI/LFM2.5-VL-3B-DSpark-GGUF"));
    QVERIFY(!lfm->sha256.value(QStringLiteral("lfm2.5-vl-3b-dspark-f16.gguf")).isEmpty());
    QVERIFY(lfm->mtpRepo != lfm->repo);  // a different repo is the whole point of the test

    // The Gemma 4 drafter is the opposite case: it ships inside the model's own
    // repository (repo-root mtp-*.gguf), so the preset must resolve the draft's
    // repo to the model's — a mtpRepo left empty is fine only because the
    // install pipeline makes that substitution.
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
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("pageOcr")), QStringLiteral("unlimited-ocr"));
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("blockRecognition")), QStringLiteral("glm-ocr"));
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("decision")), QStringLiteral("clef-flash"));
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("layout")), QStringLiteral("teleocr"));
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("rawPageOcr")), QStringLiteral("glm-ocr"));
    QCOMPARE(ModelProfiles::defaultIdForRole(profiles, QStringLiteral("fix")), QStringLiteral("qwen3.5-4b"));

    // Exactly one default per task: the auto-pipeline of the next step picks
    // "the default model for the role" and must never face a tie or a gap
    // that would silently fall back to the first-in-list.
    const QList<const char *> tasks = {"pageOcr", "rawPageOcr", "blockRecognition", "decision", "layout", "fix"};
    for (const char *task : tasks) {
        const QString role = QString::fromUtf8(task);
        int flagged = 0;
        for (const ModelProfiles::Profile &profile : profiles) {
            const ModelProfiles::Role *modelRole = ModelProfiles::roleFor(profile, role);
            if ((modelRole && modelRole->isDefault) || (profile.isDefault && modelRole))
                ++flagged;
        }
        QCOMPARE(flagged, 1);
    }

    // The installer's picker sorts the role's recommended model first.
    QCOMPARE(ModelProfiles::forRole(profiles, QStringLiteral("blockRecognition")).constFirst().id, QStringLiteral("glm-ocr"));
}

// Every check profile ships the verifier system prompt in the model's own
// idiom; a role without one — and an unknown model — resolves to nothing.
void TestModelPresetCatalog::verifierSystemPromptFollowsTheModel()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();

    // The block-recognition contract: transcribe the crop, reply with the
    // block text alone.
    const QString lfm = ModelProfiles::systemPromptFor(profiles, QStringLiteral("lfm25-vl-3b"), QStringLiteral("blockRecognition"));
    QVERIFY(lfm.contains(QStringLiteral("transcribe one block")));
    QVERIFY(lfm.contains(QStringLiteral("the block text and nothing else")));

    // teleocr ships the card's minimal per-block prompts, so the reply
    // contract lives in its system prompt.
    const QString tele = ModelProfiles::systemPromptFor(profiles, QStringLiteral("teleocr"), QStringLiteral("blockRecognition"));
    QVERIFY(tele.contains(QStringLiteral("the block text and nothing else")));

    // The decision role carries its own wording (the /v1/systemone
    // instruction): the state's notation disclaimer travels in statePrompt,
    // the ask in question.
    const QString decision = ModelProfiles::statePromptFor(profiles, QStringLiteral("clef-flash"), QStringLiteral("decision"));
    QVERIFY(decision.contains(QStringLiteral("carrier, not content")));
    QVERIFY(ModelProfiles::questionFor(profiles, QStringLiteral("clef-flash"), QStringLiteral("decision")).contains(QStringLiteral("exactly the same characters, words, numbers and symbols")));

    // A page OCR role has no system prompt, and neither has an unknown model.
    QVERIFY(ModelProfiles::systemPromptFor(profiles, QStringLiteral("lfm25-vl-3b"), QStringLiteral("pageOcr")).isEmpty());
    QVERIFY(ModelProfiles::systemPromptFor(profiles, QStringLiteral("unknown"), QStringLiteral("blockRecognition")).isEmpty());
}

// The fix task's profiles speak the OK / FIX / REVIEW correction protocol: the
// system prompt pins the three-token contract, the per-type prompts describe
// what a FIX may change, and the recheck framing is gone — the candidate text
// travels as its own request part (ADR 163).
void TestModelPresetCatalog::fixRolesShipTheCorrectionProtocol()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    const QStringList fixModels = ModelProfiles::idsForRole(profiles, QStringLiteral("fix"));
    QCOMPARE(fixModels, QStringList({QStringLiteral("gemma-4-12b"), QStringLiteral("gemma-4-e4b"), QStringLiteral("qwen3.5-4b"), QStringLiteral("qwen3.5-9b")}));

    for (const QString &id : fixModels) {
        const ModelProfiles::Role *fix = ModelProfiles::roleFor(id, QStringLiteral("fix"));
        QVERIFY2(fix, qPrintable(id));
        const QString system = fix->systemPrompt;
        QVERIFY2(system.contains(QStringLiteral("Output exactly one of")), qPrintable(id));
        QVERIFY2(system.contains(QStringLiteral("OK")), qPrintable(id));
        QVERIFY2(system.contains(QStringLiteral("FIX")), qPrintable(id));
        QVERIFY2(system.contains(QStringLiteral("REVIEW")), qPrintable(id));
        QVERIFY2(!fix->blockPrompts.isEmpty(), qPrintable(id));
        for (auto it = fix->blockPrompts.constBegin(); it != fix->blockPrompts.constEnd(); ++it)
            QVERIFY2(it.value().contains(QStringLiteral("FIX")), qPrintable(QStringLiteral("%1: %2").arg(id, it.key())));
    }
}

// The layout role is what the manual markup pass drives: its parser id must be
// the registered TeleOCR layout parser, the prompt is the card's layout
// request, and the system prompt pins the reply contract the parser expects.
void TestModelPresetCatalog::layoutRoleShipsTheMarkupContract()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();

    const ModelProfiles::Role *role = ModelProfiles::roleFor(QStringLiteral("teleocr"), QStringLiteral("layout"));
    QVERIFY2(role, "teleocr declares the layout role");
    QCOMPARE(role->parser, QStringLiteral("teleocr-layout"));
    QVERIFY(!role->prompts.isEmpty());
    QVERIFY(role->prompts.constFirst().text.contains(QStringLiteral("Analyze the image layout.")));
    QVERIFY(role->systemPrompt.contains(QStringLiteral("<box:")));
    QVERIFY(role->systemPrompt.contains(QStringLiteral("<label:")));

    // Every other shipped model serves the one-shot reply shape and hides the
    // markup button.
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("unlimited-ocr"), QStringLiteral("layout")));
    QVERIFY(!ModelProfiles::roleFor(QStringLiteral("lfm25-vl-3b"), QStringLiteral("layout")));
}

// After the markup pass every non-image block goes through blockRecognition,
// and its type prompt comes from this map: a label the layout styles but the
// block-recognition role does not name would send an empty prompt.
void TestModelPresetCatalog::teleOcrBlockPromptsCoverTheLayoutVocabulary()
{
    // The label→style map lives on the layout role (BlockStyleMap reads it there).
    const ModelProfiles::Role *layout = ModelProfiles::roleFor(QStringLiteral("teleocr"), QStringLiteral("layout"));
    const ModelProfiles::Role *check = ModelProfiles::roleFor(QStringLiteral("teleocr"), QStringLiteral("blockRecognition"));
    QVERIFY(layout);
    QVERIFY(check);
    for (auto it = layout->blockStyles.constBegin(); it != layout->blockStyles.constEnd(); ++it) {
        if (it.value() == QLatin1String("image"))
            continue;  // image placeholders are never transcribed
        QVERIFY2(check->blockPrompts.contains(it.key()), qPrintable(QStringLiteral("no block prompt for %1").arg(it.key())));
        QVERIFY2(!check->blockPrompts.value(it.key()).isEmpty(), qPrintable(QStringLiteral("empty block prompt for %1").arg(it.key())));
    }
}

void TestModelPresetCatalog::checkModelBlockPromptsOverridePerType()
{
    const ModelProfiles::Role *role = ModelProfiles::roleFor(QStringLiteral("teleocr"), QStringLiteral("blockRecognition"));
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

    // The per-type prompts are the model card's own request examples (text,
    // table, formula, code); types without a card prompt use the text one.
    QCOMPARE(role->blockPrompts.value(QStringLiteral("text")), QStringLiteral("Please output the text content from the image."));
    QCOMPARE(role->blockPrompts.value(QStringLiteral("formula")), QStringLiteral("Please write out the expression of the formula in the image using LaTeX format."));
    QCOMPARE(role->blockPrompts.value(QStringLiteral("code")), QStringLiteral("The image contains a code snippet, please output the parsing result."));
    // Tables: the model card asks for OTSL and converts client-side, but the
    // block-recognition reply IS the block text and no parser handles OTSL —
    // the override must ask for the format the app can use.
    QVERIFY(role->blockPrompts.value(QStringLiteral("table")).contains(QStringLiteral("HTML format")));
    QVERIFY(!role->blockPrompts.value(QStringLiteral("table")).contains(QStringLiteral("OTSL format")));

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
    QVERIFY(ModelProfiles::blockPromptFor(profiles, QStringLiteral("teleocr"), QStringLiteral("blockRecognition"), QStringLiteral("unknown-type")).isEmpty());
    QVERIFY(!ModelProfiles::blockPromptFor(profiles, QStringLiteral("teleocr"), QStringLiteral("blockRecognition"), QStringLiteral("formula")).isEmpty());
    QVERIFY(ModelProfiles::blockPromptFor(profiles, QStringLiteral("some-guf"), QStringLiteral("blockRecognition"), QStringLiteral("formula")).isEmpty());
    // The page OCR task of the same model has no block prompts at all.
    QVERIFY(ModelProfiles::blockPromptFor(profiles, QStringLiteral("unlimited-ocr"), QStringLiteral("pageOcr"), QStringLiteral("formula")).isEmpty());
}

// The decision gate asks one question over a structured state (docs/17): the
// profile carries the state's notation disclaimer in statePrompt and the ask
// in question. The measured scheme ships neither the noul answer sides nor
// per-type check lists — neither improved the separation — and the mechanisms
// stay available to profiles that want them. Every System One model ships the
// same wording: the d3 family reads the same request schema as clef-flash.
void TestModelPresetCatalog::decisionRoleShipsTheStructuredStateWording()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    const QStringList decisionModels = {QStringLiteral("clef-flash"), QStringLiteral("d3-flash"), QStringLiteral("d3-mini")};
    for (const QString &id : decisionModels) {
        const ModelProfiles::Role *role = ModelProfiles::roleFor(id, QStringLiteral("decision"));
        QVERIFY2(role, qPrintable(QStringLiteral("%1 declares the decision role").arg(id)));

        QVERIFY2(role->statePrompt.contains(QStringLiteral("carrier, not content")), qPrintable(id));
        QVERIFY2(role->question.contains(QStringLiteral("exactly the same characters, words, numbers and symbols")), qPrintable(id));
        QVERIFY2(role->criteriaTrue.isEmpty(), qPrintable(id));
        QVERIFY2(role->criteriaFalse.isEmpty(), qPrintable(id));
        QVERIFY2(role->blockPrompts.isEmpty(), qPrintable(id));
        QCOMPARE(ModelProfiles::criteriaTrueFor(profiles, id, QStringLiteral("decision")), QString());
        QCOMPARE(ModelProfiles::blockPromptFor(profiles, id, QStringLiteral("decision"), QStringLiteral("formula")), QString());
    }
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
    // downloads (TeleOCR carried a note until its repository moved to weights
    // that load on a stock build).
    QVERIFY(ModelProfiles::runtimeNoteFor(profiles, profiles.last().id).isEmpty());
    QVERIFY(ModelProfiles::runtimeNoteFor(profiles, QStringLiteral("some-guf")).isEmpty());
}

void TestModelPresetCatalog::mergeByUserPrecedence()
{
    // Build a user catalog that overrides one built-in id and adds a new one.
    QList<ModelPreset> user;
    ModelPreset override_;
    override_.id = QStringLiteral("unlimited-ocr-q4km");
    override_.title = QStringLiteral("User-tuned Unlimited-OCR");
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
            QCOMPARE(p.title, QStringLiteral("User-tuned Unlimited-OCR"));
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