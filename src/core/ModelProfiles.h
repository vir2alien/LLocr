#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "core/LaunchProfile.h"
#include "core/RequestProfile.h"

namespace llocr {

class ModelProfiles
{
public:
    struct Prompt {
        QString id;
        QString title;
        QString text;
    };

    struct DecisionBlock {
        QString statePrompt;  // per-block equivalence rules, carried into state.notation
        QString question;     // legacy single-question wording (scheme v5)
    };

    struct Role {
        QString alias;
        QString parser;
        QString protocol;  // reply contract id ("full_block" fix, "systemone_multi_question_v6" decision)
        int maxOutput = 0;
        QHash<QString, int> blockMaxOutput;  // maxOutputByBlock, keyed like blockPrompts (fix task)
        bool isDefault = false;              // the role's recommended model, replacing the first-in-catalog fallback
        QString systemPrompt;                // check-role override of the shared verifier system prompt
        QString statePrompt;                 // decision role: framing + comparison rules; read instead of systemPrompt
        QString question;                    // decision role: the yes/no question itself when the profile ships one
        QString criteriaTrue;                // decision role: the "true" side of the noul question (what counts as a match)
        QString criteriaFalse;               // decision role: the "false" side (what counts as a difference)
        QList<Prompt> prompts;
        QList<LaunchParameter> launch;
        QList<RequestParameter> request;
        QHash<QString, QString> blockStyles;
        QHash<QString, QString> blockPrompts;
        QHash<QString, DecisionBlock> decisionBlocks;  // blockPrompts entries shipped as objects (decision v6)
        double matchThreshold = -1.0;                  // -1 = not shipped; the settings knob applies
        QHash<QString, double> blockMatchThresholds;   // matchThresholdByBlock, keyed like blockPrompts
        double emptyThreshold = -1.0;
        double readableThreshold = -1.0;
        QHash<QString, QString> blockAliases;  // alias -> canonical blockPrompts key
        bool promptBeforeImage = false;        // wire order: prompt text first, then the image (LFM2.5 trains text-then-image)
        int imageLongestEdge = 0;              // 0 = send the page as-is; >0 = downscale to fit (LightOnOCR-3 trains on 2048 px pages)
        QString promptProfile;                 // id of the prompt profile this role takes its texts from
        bool reasoningSupported = false;       // the model can think: the fix task may spend a reasoning budget
    };

    struct Quant {
        QString id;
        QString file;
        QString sha256;
        qint64 sizeBytes = 0;  // curated download size (ADR 168), shown before install
    };

    struct Module {
        QString id;
        QString file;
        QString sha256;
        QString repo;          // empty = files.repo; the DSpark/MTP draft lives in its own repo
        QString revision;      // empty = pin the repo head at install time
        qint64 sizeBytes = 0;  // curated download size (the projector's own weight)
    };

    // The architecture the memory estimate needs (ADR 191). A quantization
    // that is not downloaded yet has no GGUF header to read, so the profile
    // ships the shape: the requirement must be known before the file exists.
    struct Memory {
        int nLayer = 0;     // decoder layers
        int nKvLayers = 0;  // layers whose cache follows the context (0 = all of them)
        int nHead = 0;
        int nKvHead = 0;
        int headDim = 0;
        int nEmbd = 0;
        int nFf = 0;
        int nMoeLayers = 0;              // the layers that route to experts
        int nExpertUsed = 0;             // experts a token activates
        int nExpertFf = 0;               // the expert's own feed-forward width
        int visionTokens = 0;            // the image tokens the server must hold
        qint64 visionBytesPerToken = 0;  // the encoder's working set per image token
        qint64 visionBufferBytes = 0;    // the encoder's whole working set (0 = follow the token count)
        qint64 draftBytes = 0;           // the speculative decoding draft the server loads next to the target
        // A draft that is a model of its own (a separate GGUF) runs in its own
        // context: it holds a KV cache and a compute buffer of its own.
        qint64 draftKvBytesPerToken = 0;  // that context's KV cache per context token
        qint64 draftContextBytes = 0;     // what it holds besides that (the sliding-window cells, the compute buffer)
        // llama.cpp's own numbers, where its scheduler reserves more than the
        // shape predicts: the measured cost of one token of the graph's
        // working set (0 = derive it from the shape) and of one context token
        // of the attention mask.
        qint64 workingSetBytesPerToken = 0;
        qint64 workingSetContextBytes = 0;
        qint64 kvBytesPerToken = 0;  // the KV cache per context token (0 = derive it)
        qint64 kvBytesFixed = 0;     // the sliding layers' cells (they do not follow the context)
        bool valid = false;
    };

    struct Files {
        QString repo;
        QString revision;  // may be empty; the install pins the repo head
        QList<Quant> quants;
        Module mmproj;
        Module mtp;  // optional speculative-decoding draft module (DSpark / MTP)
    };

    struct Profile {
        QString id;
        QString title;
        QString minBuild;
        QString license;
        QString runtimeNote;
        bool isDefault = false;
        Files files;
        Memory memory;
        QHash<QString, Role> roles;
    };

    // The request texts of a task, kept in their own profile and shared by
    // every model that serves the task (ADR 182): the system prompt, the
    // per-block prompts and the alias map. A model role names the
    // profile it speaks ("promptProfile": "fix-requests"). The struct is a
    // "prompt profile" and not a "request profile" because the latter already
    // means the request parameters (RequestProfile, RequestProfileStore).
    struct PromptProfile {
        QString id;
        QString title;
        Role prompts;
        // The reasoning modes the task offers (the label and the budget of
        // thinking tokens). The fix task's modes are shared by every model
        // that serves it; a model profile says whether its model can think at
        // all (ADR 193).
        QList<RequestParameterChoice> reasoningModes;
    };

    static constexpr const char *kResourceDir = ":/profiles/models";
    static constexpr const char *kPromptResourceDir = ":/profiles/requests";

    static QList<Profile> loadFrom(const QString &directory, QString &error);
    static QList<Profile> loadBuiltIn(QString &error);

    static QList<PromptProfile> loadPromptProfilesFrom(const QString &directory, QString &error);
    static const QList<PromptProfile> &promptProfiles();
    static const PromptProfile *promptProfile(const QString &id);

    static const QList<Profile> &instance();

    static const Profile *find(const QList<Profile> &profiles, const QString &id);

    // The built-in profile with that id, from the index of the built-in list.
    static const Profile *findBuiltIn(const QString &id);
    static const Role *roleFor(const Profile &profile, const QString &role);
    static const Role *roleFor(const QString &modelId, const QString &role);

    static QString idForRepo(const QList<Profile> &profiles, const QString &repo);

    // The quantization a profile declares for one of its files. The file name
    // often carries no quant token (PaddleOCR-VL ships "…-GGUF.gguf"), so the
    // profile is the only place that knows the quant; empty when neither the
    // repo nor the file is known.
    static QString quantIdForFile(const QString &repo, const QString &fileName);

    static QString defaultIdForRole(const QList<Profile> &profiles, const QString &role);

    static QStringList idsForRole(const QList<Profile> &profiles, const QString &role);

    static QList<Profile> forRole(const QList<Profile> &profiles, const QString &role);

    static QList<LaunchParameter> launchFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);
    static QList<RequestParameter> requestFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);

    static QString blockPromptFor(const QList<Profile> &profiles, const QString &modelId, const QString &role, const QString &type);

    static DecisionBlock decisionBlockFor(const QList<Profile> &profiles, const QString &modelId, const QString &role, const QString &type);

    static QString systemPromptFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);

    static QString statePromptFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);

    static QString questionFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);

    static QString criteriaTrueFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);
    static QString criteriaFalseFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);

    static QString runtimeNoteFor(const QList<Profile> &profiles, const QString &modelId);

    static QList<RequestParameter> requestWithMaxOutput(const QList<Profile> &profiles, const QString &modelId, const QString &role);

    // Per-block output budget of the fix task: maxOutputByBlock[type] (through
    // the block aliases), else -1 when the profile ships no per-block entry.
    static int maxOutputForBlock(const QList<Profile> &profiles, const QString &modelId, const QString &role, const QString &blockType);

    // The image budget of a task: the number of image tokens the server has to
    // hold. A page fills the model's own budget (the model shrinks the page
    // to its image-max-tokens), a block crop is scaled up to the floor the
    // model declares (its image-min-tokens) and stays small otherwise.
    static int visionTokensFor(const Profile &profile, const QString &role);
};

}  // namespace llocr
