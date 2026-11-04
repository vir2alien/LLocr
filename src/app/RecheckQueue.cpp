#include "app/RecheckQueue.h"

#include "config/RequestProfileStore.h"
#include "core/ModelProfiles.h"

namespace llocr {

RecheckQueue::RecheckQueue(Deps deps, QObject *parent)
    : BlockQueue({deps.document, deps.gate, deps.cropProvider}, parent), m_deps(std::move(deps)), m_check(m_deps.checkRequestProfiles, m_deps.runtime, nullptr)
{
    connect(&m_check, &CheckController::checkFinished, this, [this](const CheckResult &result) {
        if (result.status == CheckStatus::Failed && !result.errorMessage.isEmpty())
            reportNewError(result.errorMessage);
        emit blockRecognized(taskPage(), taskBox(), result);
        advance();
    });
    connect(&m_check, &CheckController::busyChanged, this, &RecheckQueue::stateChanged);
    connect(&m_check, &CheckController::statusRequested, this, &RecheckQueue::statusRequested);
}

bool RecheckQueue::requestBusy() const
{
    return m_check.busy();
}

void RecheckQueue::startTask(const Task &task, const BoundingBox &box, const QImage &crop, const QString &text)
{
    const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
    const QString userPrompt = ModelProfiles::blockPromptFor(ModelProfiles::instance(), modelId, QStringLiteral("blockRecognition"), box.label);
    const QString systemPrompt = ModelProfiles::systemPromptFor(ModelProfiles::instance(), modelId, QStringLiteral("blockRecognition"));
    m_check.checkBlock(crop, systemPrompt, userPrompt);
}

void RecheckQueue::stopRequest()
{
    m_check.stop();
}

}  // namespace llocr
