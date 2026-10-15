#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "core/StatusMessage.h"

namespace llocr {

enum class BoxCheckStatus : int {
    NotChecked = 0,  ///< No verdict yet. With text: recognized (gray dot);
                     ///< without text: layout markup (no dot).
    Ok = 1,          ///< The decision model confirmed the text matches the image.
                     ///< 2 is reserved: pre-v2 projects stored ReRecognized there,
                     ///< read back as NotChecked (ADR 164).
    Review = 3,      ///< The block is unreadable — needs human eyes.
    Mismatch = 4,    ///< The decision model rejected the text.
};

// The single int->enum gate: project load and the QML write path must not
// drift into accepting different ranges (an out-of-range value would reach
// project.json and silently coerce back to NotChecked on the next open).
inline BoxCheckStatus boxCheckStatusFromInt(int value)
{
    if (value == 2)  // the reserved ReRecognized slot of v1 projects
        return BoxCheckStatus::NotChecked;
    return value >= int(BoxCheckStatus::NotChecked) && value <= int(BoxCheckStatus::Mismatch) ? BoxCheckStatus(value) : BoxCheckStatus::NotChecked;
}

struct BoundingBox {
    QString text;                                                      ///< OCR result of this fragment (the text layer).
    QString correctedText;                                             ///< Correction layer: the universal model's fix (ADR 163);
                                                                       ///< empty when the block was never corrected.
    BoxCheckStatus checkStatus = BoxCheckStatus::NotChecked;           ///< Verdict on the text layer.
    BoxCheckStatus correctedCheckStatus = BoxCheckStatus::NotChecked;  ///< Verdict on the correction layer.
    QString label;                                                     ///< Block type reported by the model (title, text, table...).
    QRectF rect;                                                       ///< Normalized rectangle: x, y, width, height in [0, 1].
    bool positioned = true;
    bool duplicateSuspect = false;      ///< A duplicate was merged into this block —
                                        ///< included in verification regardless of the
                                        ///< label filter.
    double confidence = -1.0;           ///< P(match) of the text-layer verdict; -1 = none. Debug aid, not persisted.
    double correctedConfidence = -1.0;  ///< P(match) of the correction-layer verdict; -1 = none. Debug aid, not persisted.
};

struct OcrPage {
    QString text;                ///< Full text of this page (Markdown-friendly).
    QList<BoundingBox> boxes;    ///< Optional positioned fragments for this page.
    bool hasDuplicates = false;  ///< True when at least one duplicate bbox was detected & replaced.
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