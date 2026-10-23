#include "core/RequestBudget.h"

#include <QtGlobal>

namespace llocr {

int RequestBudget::room() const
{
    if (contextSize <= 0)
        return answerTokens + reasoningTokens;  // the context is unknown: nothing to clip
    return qMax(0, contextSize - promptTokens);
}

int RequestBudget::effectiveAnswer() const
{
    return qMin(answerTokens, room());
}

int RequestBudget::effectiveReasoning() const
{
    return qMin(reasoningTokens, qMax(0, room() - effectiveAnswer()));
}

int RequestBudget::maxTokens() const
{
    return effectiveAnswer() + effectiveReasoning();
}

int estimatePromptTokens(const QString &systemPrompt, const QString &typePrompt, const QString &candidate, int imageTokens)
{
    const QString text = systemPrompt + typePrompt + candidate;
    int cyrillic = 0;
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code >= 0x0400 && code <= 0x04FF)
            ++cyrillic;
    }
    const int other = text.size() - cyrillic;
    return imageTokens + cyrillic / 2 + other / 4;
}

}  // namespace llocr
