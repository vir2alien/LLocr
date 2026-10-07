#include "parsers/Lfm25VlParser.h"

#include "parsers/Lfm25VlDrift.h"

namespace llocr {

namespace {

const QRegularExpression &annotationTokenRegex()
{
    static const QRegularExpression re(QStringLiteral("(?|"
                                                      "(?:image_index=\\d+\\s+)?([A-Za-z_][A-Za-z0-9_]*)\\s*\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\]"
                                                      "|"
                                                      "image_index=\\d+\\s+<label>([A-Za-z_][A-Za-z0-9_]*)</label>(?:\\s*\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\])?"
                                                      ")"));
    return re;
}

// Measured on real replies: the model grounds on
// a 3:2-canvas letterbox of the page — the width fills the canvas, the height
// is centered at (H/W)/1.5 of it (an A4 page: model_y = 0.945·true_y + 27.5
// of 1000, x exact). The inverse map restores page-true rects; pages taller
// than 3:2 letterbox the width instead.
constexpr qreal kGroundAspect = 1.5;

}  // namespace

QString Lfm25VlParser::id() const
{
    return QStringLiteral("lfm2.5-vl");
}

QString Lfm25VlParser::displayName() const
{
    return QStringLiteral("LFM2.5-VL");
}

const QRegularExpression &Lfm25VlParser::tokenRegex(const QString &) const
{
    return annotationTokenRegex();
}

bool Lfm25VlParser::escapesLineBreaks(const QString &) const
{
    return false;  // the annotation stream carries real newlines
}

QString Lfm25VlParser::prepareText(const QString &rawText) const
{
    return normalizeDriftRegions(rawText);
}

QRectF Lfm25VlParser::calibrateRect(const QRectF &rect) const
{
    if (rect.isNull())  // unpositioned fragments share the zero rect
        return rect;
    const qreal aspect = m_options.pageAspect > 0 ? m_options.pageAspect : 1.4142;
    qreal xInset = 0.0, xScale = 1.0, yInset = 0.0, yScale = 1.0;
    if (aspect < kGroundAspect) {
        yScale = aspect / kGroundAspect;
        yInset = (1.0 - yScale) / 2.0;
    } else {
        xScale = kGroundAspect / aspect;
        xInset = (1.0 - xScale) / 2.0;
    }
    const QPointF topLeft((rect.left() - xInset) / xScale, (rect.top() - yInset) / yScale);
    const QPointF bottomRight((rect.right() - xInset) / xScale, (rect.bottom() - yInset) / yScale);
    return QRectF(topLeft, bottomRight).normalized() & QRectF(0, 0, 1, 1);
}

}  // namespace llocr
