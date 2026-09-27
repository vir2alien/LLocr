#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QRectF>

#include "core/StatusMessage.h"

namespace llocr {

enum class BoxCheckStatus : int {
    NotChecked = 0,  ///< Verification has not run for this block yet.
    Ok = 1,          ///< Verification passed — the recognized text is correct.
    Fixed = 2,       ///< The verifier returned a FIX; correctedText holds the result.
    Review = 3,      ///< The verifier returned REVIEW — the block is unreadable.
};

struct BoundingBox {
    QString text;             ///< Recognized text of this fragment.
    QString correctedText;    ///< Verified text (when checkStatus == Fixed); kept separate from text.
    BoxCheckStatus checkStatus = BoxCheckStatus::NotChecked;
    QString label;            ///< Block type reported by the model (title, text, table...).
    QRectF rect;              ///< Normalized rectangle: x, y, width, height in [0, 1].
    double confidence = 0.0;  ///< Optional model confidence, if provided.
    /// False when the model did not place this fragment (an XML-drift header
    /// without coordinates, an untagged preamble): its rect is (0,0,0,0) and it
    /// must never take part in duplicate-region detection.
    bool positioned = true;
};

struct OcrPage {
    QString text;              ///< Full text of this page (Markdown-friendly).
    QList<BoundingBox> boxes;  ///< Optional positioned fragments for this page.
    bool hasDuplicates = false; ///< True when at least one duplicate bbox was detected & replaced.
};

struct OcrResult {
    bool success = false;    ///< Whether recognition succeeded.
    QString text;            ///< Flat text of all pages (joined).
    QList<OcrPage> pages;    ///< Structured per-page result.
    /// Human-readable error when success == false. Untranslated until read, so
    /// it follows the UI language (ADR 114).
    StatusMessage errorMessage;
    QStringList notes;       ///< Non-fatal parser diagnostics (page kept, but
                             ///< something looked off — e.g. no layout tokens).

    static OcrResult makeError(const StatusMessage& message) {
        OcrResult result;
        result.success = false;
        result.errorMessage = message;
        return result;
    }
};

} // namespace llocr