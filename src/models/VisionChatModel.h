#pragma once

#include <memory>
#include <QByteArray>
#include <QFuture>
#include <QJsonArray>
#include <QJsonObject>

#include "core/CheckRequest.h"
#include "core/CheckResult.h"
#include "core/ConnectionConfig.h"
#include "core/LlamaClient.h"

namespace llocr {

// Shared transport of the block-stage tasks (block recognition, fix, layout
// markup): one vision chat-completions request over LlamaClient, one client
// per model lifetime so the requests of a task ride one keep-alive
// connection. The task-specific wire shape and reply contract live in the
// subclasses (BlockRecognitionModel, FixModel, LayoutModel); the decision
// task keeps its own endpoint and model (DecisionModel).
class VisionChatModel
{
    Q_DISABLE_COPY_MOVE(VisionChatModel)

public:
    VisionChatModel() = default;
    ~VisionChatModel() = default;

    void abort();

    // Reply hygiene for every vision task: a Markdown-trained model escapes the
    // punctuation that follows inline math ("$x$\." "$x$\,") — the backslash
    // prints literally in the transcript (LightOnOCR's own client applies the
    // same fix as its fix_dollars).
    static QString stripEscapedPunctuation(const QString &text);

protected:
    // The request root every task sends: the model name, the sampling
    // parameters from the task's request profile, and the template switch
    // that keeps reasoning-format models from wrapping replies in thinking.
    static QJsonObject buildRoot(const CheckRequest &request, QJsonArray messages);
    static QJsonObject textPart(const QString &text);
    static QJsonObject imagePart(const QByteArray &imageDataUrl);

    // The chat-completions envelope every task reads: choices[0].message.content.
    // On a malformed envelope *error carries the failure and false is returned;
    // an empty content is a valid reply.
    static bool replyContent(const QByteArray &responseData, QString *content, CheckResult *error);

    // What the server actually said when the reply carries no answer: its
    // error payload, the thinking that spent the budget, or the raw body.
    // "No choices" on its own tells the user nothing about what to fix.
    static QString missingAnswerReason(const QByteArray &responseData);

    // Reply hygiene: a whole-reply Markdown fence is packaging, not content;
    // control tokens (<|...|> service markers, thinking blocks) are model
    // chatter around the text. The structural layout reply skips the control
    // pass — its <|...|> markers are the data a parser reads.
    static QString stripControlTokens(const QString &text);
    static QString stripOuterCodeFence(const QString &text);

    std::shared_ptr<LlamaClient> m_activeClient = std::make_shared<LlamaClient>();
};

}  // namespace llocr
