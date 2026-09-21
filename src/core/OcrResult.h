#pragma once

#include <QList>
#include <QString>
#include <QRectF>

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
    QString errorMessage;    ///< Human-readable error when success == false.

    static OcrResult makeError(const QString& message) {
        OcrResult result;
        result.success = false;
        result.errorMessage = message;
        return result;
    }
};

} // namespace llocr