#include "app/FixController.h"

#include "config/RequestProfileStore.h"
#include "core/RequestBudget.h"

namespace llocr {

namespace {

// The vision floor of the shipped models (their image-min-tokens runs from 70
// to 1120). An external server's budget is unknown, and the estimate has to
// err on the safe side: a request that does not fit the window is aborted by
// the server, and the model is unloaded with it.
constexpr int kUnknownImageTokens = 1024;

int launchNumber(const QList<LaunchParameter> &parameters, const QString &name)
{
    for (const LaunchParameter &parameter : parameters) {
        if (parameter.name == name && parameter.kind == LaunchValueKind::Number)
            return parameter.value.toInt();
    }
    return 0;
}

int parameterNumber(const QList<RequestParameter> &parameters, const QString &name)
{
    for (const RequestParameter &parameter : parameters) {
        if (parameter.name == name)
            return parameter.value.toInt();
    }
    return 0;
}

void setParameterNumber(QList<RequestParameter> &parameters, const QString &name, int value)
{
    for (RequestParameter &parameter : parameters) {
        if (parameter.name == name) {
            parameter.value = QVariant(double(value));
            return;
        }
    }
}

}  // namespace

FixController::FixController(RequestProfileStore &requestProfiles, RuntimeController &runtime, QObject *parent)
    : ModelRequestController(runtime, "FixController", parent), m_requestProfiles(requestProfiles), m_model(std::make_unique<FixModel>())
{
    connect(&m_watcher, &QFutureWatcher<CheckResult>::finished, this, [this]() {
        if (stopRequested()) {
            emit statusRequested(StatusMessage::translate(trContext(), "Fix stopped."));
            finish();
            return;
        }
        CheckResult result = watcherResult(m_watcher);
        if (!m_budgetNote.isEmpty()) {
            QList<StatusMessage> parts;
            if (!result.notes.isEmpty())
                parts.append(result.notes);
            parts.append(m_budgetNote);
            result.notes = StatusMessage::join(parts);
        }
        emit fixFinished(result);
        finish();
    });
}

void FixController::abortRequest()
{
    if (m_model)
        m_model->abort();
}

QList<RequestParameter> FixController::requestParameters() const
{
    return m_requestProfiles.activeProfile().parameters;
}

void FixController::fixBlock(const QImage &image, const QString &recognizedText, const QString &systemPrompt, const QString &typePrompt, const QString &blockType, int maxOutput)
{
    if (busy() || image.isNull())
        return;

    setBusy(true);

    beginRequest(
        ConnectionRole::Fix,
        StatusMessage::translate(trContext(), "Stopped before the fix started."),
        [this, image, recognizedText, systemPrompt, typePrompt, blockType, maxOutput](const ResolvedConnection &conn) {
            CheckRequest request;
            request.image = image;
            request.systemPrompt = systemPrompt;
            request.typePrompt = typePrompt;
            request.recognizedText = recognizedText;
            request.blockType = blockType;
            request.modelId = conn.modelId;
            request.parameters = requestParameters();
            // The per-block budget overrides the profile's max_tokens only
            // where the profile ships a per-block entry; a user-edited value
            // stays untouched for the other types.
            if (maxOutput > 0)
                setParameterNumber(request.parameters, QStringLiteral("max_tokens"), maxOutput);

            // The generation budget inside the server's context window: the
            // answer's room and the thinking's room. The context is a hard
            // limit, so the thinking yields before the answer (ADR 193). The
            // managed server's window comes from the launch config; an
            // external server publishes its own in /v1/models.
            const ServerLaunchConfig &config = runtime().startedConfig();
            RequestBudget budget;
            budget.contextSize = launchNumber(config.parameters, QStringLiteral("ctx-size"));
            if (budget.contextSize <= 0)
                budget.contextSize = conn.contextSize;
            int imageTokens = launchNumber(config.parameters, QStringLiteral("image-min-tokens"));
            if (imageTokens <= 0) {
                // An external server's model budget is unknown: the app counts
                // the vision floor of the shipped models (70…1120 tokens), so
                // the estimate errs on the safe side — a request that does not
                // fit the window is aborted by the server, and the model is
                // unloaded with it.
                imageTokens = kUnknownImageTokens;
            }
            budget.promptTokens = estimatePromptTokens(systemPrompt, typePrompt, recognizedText, imageTokens);
            budget.answerTokens = parameterNumber(request.parameters, QStringLiteral("max_tokens"));
            budget.reasoningTokens = parameterNumber(request.parameters, QLatin1String(kReasoningBudgetParameter));
            setParameterNumber(request.parameters, QStringLiteral("max_tokens"), budget.maxTokens());
            setParameterNumber(request.parameters, QLatin1String(kReasoningBudgetParameter), budget.effectiveReasoning());
            m_budgetNote = StatusMessage();
            if (budget.reasoningTokens > 0 && budget.effectiveReasoning() < budget.reasoningTokens) {
                // The context did not leave room for the chosen thinking: the
                // thinking yields, the correction keeps its room.
                m_budgetNote = StatusMessage::translate("FixController", "The thinking budget was cut to %1 tokens to fit the context window.").arg(budget.effectiveReasoning());
            }
            request.task = CheckRequest::Task::Fix;

            m_watcher.setFuture(m_model->check(request, conn.toConnectionConfig()));
        },
        [this](const StatusMessage &message) {
            emit fixFinished(CheckResult::makeError(message));
            finish();
        });
}

}  // namespace llocr
