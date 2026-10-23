#pragma once

#include <QString>

namespace llocr {

// The generation budget of one request (ADR 193): what the answer gets and
// what the thinking gets inside the server's context window. The context is a
// hard limit — the prompt takes its part first, and the thinking yields
// before the answer, because a correction matters more than a thought.
struct RequestBudget {
    int contextSize = 0;      // the server's context window; 0 = unknown
    int promptTokens = 0;     // the prompt's estimated size
    int answerTokens = 0;     // the answer's budget (the block's budget)
    int reasoningTokens = 0;  // the thinking's budget (the chosen mode)

    // What the context leaves for the generation.
    int room() const;
    // The answer's room: its budget, clipped to the room.
    int effectiveAnswer() const;
    // The thinking's room: the mode's budget, clipped to what the answer left.
    int effectiveReasoning() const;
    // The generation budget: the answer plus the thinking.
    int maxTokens() const;
};

// The prompt's size in tokens, as far as the app can tell without the
// tokenizer: the text at two characters per token for Cyrillic and four for
// the rest (Qwen's vocabulary), and the image at the model's image-token
// floor.
int estimatePromptTokens(const QString &systemPrompt, const QString &typePrompt, const QString &candidate, int imageTokens);

}  // namespace llocr
