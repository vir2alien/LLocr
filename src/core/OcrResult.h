#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "core/LatexText.h"
#include "core/StatusMessage.h"

namespace llocr {

enum class BoxCheckStatus : int {
    NotChecked = 0,     ///< No verdict yet. With text: recognized (gray dot);
                        ///< without text: layout markup (no dot).
    Ok = 1,             ///< The decision model confirmed the text matches the image.
                        ///< 2 is reserved: pre-v2 projects stored ReRecognized there,
                        ///< read back as NotChecked (ADR 164).
    Review = 3,         ///< The block is unreadable — needs human eyes.
    Mismatch = 4,       ///< The decision model rejected the text.
    Approved = 5,       ///< The user confirmed the block by hand (ADR 185) — the
                        ///< decision model's verdict was wrong.
    Edited = 6,         ///< The user wrote the text by hand (ADR 187): the text
                        ///< is theirs, not the model's and not a confirmation.
    InvalidSyntax = 7,  ///< The formula's LaTeX is broken (ADR 194): the block
                        ///< is rejected right after the recognition and no
                        ///< model is asked about it.
};

// The outcome of the last fix run (ADR 185). The correction layer alone cannot
// tell a confirmed text from a stored correction: a fix that answers «no
// correction needed» stores nothing, and a block that was never fixed stores
// nothing either.
enum class BoxFixStatus : int {
    None = 0,       ///< No fix has been run on this block.
    NotNeeded = 1,  ///< The fixer confirmed the text — nothing was corrected.
    Fixed = 2,      ///< The fixer produced a correction (correctedText holds it).
    Damaged = 3,    ///< The fixer could not read the block — human eyes needed.
};

// The single int->enum gate: project load and the QML write path must not
// drift into accepting different ranges (an out-of-range value would reach
// project.json and silently coerce back to NotChecked on the next open).
inline BoxCheckStatus boxCheckStatusFromInt(int value)
{
    if (value == 2)  // the reserved ReRecognized slot of v1 projects
        return BoxCheckStatus::NotChecked;
    return value >= int(BoxCheckStatus::NotChecked) && value <= int(BoxCheckStatus::InvalidSyntax) ? BoxCheckStatus(value) : BoxCheckStatus::NotChecked;
}

inline BoxFixStatus boxFixStatusFromInt(int value)
{
    return value >= int(BoxFixStatus::None) && value <= int(BoxFixStatus::Damaged) ? BoxFixStatus(value) : BoxFixStatus::None;
}

struct BoundingBox {
    QString text;                                                      ///< OCR result of this fragment (the text layer).
    QString correctedText;                                             ///< Correction layer: the universal model's fix (ADR 163);
                                                                       ///< empty when the block was never corrected.
    BoxCheckStatus checkStatus = BoxCheckStatus::NotChecked;           ///< Verdict on the text layer.
    BoxCheckStatus correctedCheckStatus = BoxCheckStatus::NotChecked;  ///< Verdict on the correction layer.
    BoxFixStatus fixStatus = BoxFixStatus::None;                       ///< Outcome of the last fix run (ADR 185).
    bool useCorrectedText = true;                                      ///< The panel's toggle: the correction layer is the
                                                                       ///< one the user reads (ADR 185).
    QString label;                                                     ///< Block type reported by the model (title, text, table...).
    QRectF rect;                                                       ///< Normalized rectangle: x, y, width, height in [0, 1].
    bool positioned = true;
    bool duplicateSuspect = false;                                ///< A duplicate was merged into this block —
                                                                  ///< included in verification regardless of the
                                                                  ///< label filter.
    double confidence = -1.0;                                     ///< P(match) of the text-layer verdict; -1 = none.
                                                                  ///< Persisted in project.json (ADR 173) — the
                                                                  ///< threshold calibration data of the eval stand.
    double correctedConfidence = -1.0;                            ///< P(match) of the correction-layer verdict; -1 = none.
    LatexText::Problem syntaxProblem = LatexText::Problem::None;  ///< What is wrong with a
                                                                  ///< formula's LaTeX (ADR 194). The
                                                                  ///< kind, not the message: the
                                                                  ///< wording follows the UI language.

    // The block's rotation the layout model reported (ADR 245): the direction
    // the block's text top points — 0 = up, 90 = right, 180 = down,
    // 270 = left. Zero when the model said nothing (every non-layout parser).
    // Not persisted: the page-level vote consumes it as soon as the layout
    // lands.
    int orientation = 0;

    // The verdict an approval replaced (ADR 187): «Отозвать утверждение»
    // puts it back. The comment is above the fields on purpose — a trailing
    // one would drag the whole struct's comment column to the right.
    BoxCheckStatus checkStatusBeforeApproval = BoxCheckStatus::NotChecked;
    BoxCheckStatus correctedCheckStatusBeforeApproval = BoxCheckStatus::NotChecked;
};

struct OcrPage {
    QString text;                ///< Full text of this page (Markdown-friendly).
    QList<BoundingBox> boxes;    ///< Optional positioned fragments for this page.
    bool hasDuplicates = false;  ///< True when at least one duplicate bbox was detected & replaced.

    // The turn the layout model's rotation tokens vote for (ADR 245): the
    // clockwise turn that levels the page, when one direction took at least
    // nine tenths of the blocks that voted against "up". Zero when the vote
    // split or the model said nothing. `orientationVoteShare` is the share
    // of all block lines the winning direction took (0..100) — the
    // controller refuses a suggestion the page's layout does not back.
    // Not persisted; consumed when the layout lands.
    int orientationVote = 0;
    int orientationVoteShare = 0;
};

struct BlockTextRange {
    int boxIndex = -1;
    int start = 0;
    int length = 0;
};

struct OcrResult {
    bool success = false;
    QString text;          ///< Flat text of all pages (joined).
    QList<OcrPage> pages;  ///< Structured per-page result.
    StatusMessage errorMessage;
    QStringList notes;  ///< Non-fatal parser diagnostics (page kept, but
                        ///< something looked off — e.g. no layout tokens).

    static OcrResult makeError(const StatusMessage &message)
    {
        OcrResult result;
        result.success = false;
        result.errorMessage = message;
        return result;
    }
};

}  // namespace llocr