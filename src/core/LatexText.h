#pragma once

#include <QString>

namespace llocr::LatexText {

// What is wrong with a formula's LaTeX. Only the kinds the app can establish
// without a LaTeX engine: a formula that fails one of them certainly cannot
// be rendered, and no model should be asked about it (ADR 194).
enum class Problem : int {
    None = 0,
    UnclosedBrace,          ///< "{" without its "}"
    ExtraBrace,             ///< "}" without its "{"
    UnclosedEnvironment,    ///< \begin{…} without \end{…}
    MismatchedEnvironment,  ///< \begin{a} closed by \end{b}
    UnbalancedLeftRight,    ///< \left without \right, or the other way round
    UnbalancedDelimiter,    ///< "$", "\[" or "\(" without its closer
    TruncatedCommand,       ///< a "\" at the end of the text
    MissingArgument,        ///< a command with fewer arguments than it needs
    CodeMarkup,             ///< a markdown code marker left in the formula
};

inline Problem problemFromInt(int value)
{
    return value >= int(Problem::None) && value <= int(Problem::CodeMarkup) ? Problem(value) : Problem::None;
}

// The block labels whose text is LaTeX.
bool isMathLabel(const QString &label);

// The canonical form of a formula's text: the OCR models emit spaced LaTeX
// ("Bi _ {1}"), which every model that reads it takes for noise and every
// fixer answers with pure spacing churn. The canonical form is what the block
// carries from the moment it is recognized — the wire carries the block's own
// text, not a rewritten copy of it (ADR 194).
//
// A markdown code fence is carrier markup, not formula content: the model
// answers "```latex\n…\n```", sometimes in several pieces and sometimes
// inside the math wrapper. The fence is unwrapped and the LaTeX inside is
// kept — a formula with a fence in it cannot be rendered (ADR 195).
QString canonical(const QString &text);

// What is wrong with the text, Problem::None when it is sound.
Problem check(const QString &text);

}  // namespace llocr::LatexText
