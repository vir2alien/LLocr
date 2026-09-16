#pragma once

#include <QList>
#include <QString>
#include <QRectF>

namespace llocr {

struct BoundingBox {
    QString text;             ///< Recognized text of this fragment.
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