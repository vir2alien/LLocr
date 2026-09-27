#pragma once

#include <QHash>
#include <QString>

class QJsonObject;

namespace llocr {

enum class BlockStyle {
    PlainText,
    Heading,
    ImagePlaceholder,
    Italic,
    Equation,
    Table,
};

struct BlockStyleInfo {
    BlockStyle style = BlockStyle::PlainText;
    int headingLevel = 0;
    int imageIndex = -1;  ///< Index of the block in OcrPage::boxes (for ImagePlaceholder).
};

// Label → block style. Loaded from ":/profiles/labels.json" so a model with its
// own label vocabulary needs a data edit, not a C++ change; the built-in table
// below is the fallback for builds without the resource (unit tests) and for a
// malformed file. Model-specific styling lives under "overrides" keyed by OCR
// model id and is looked up by blockStyleForLabel(label, modelId).
class BlockStyleMap
{
public:
    static const BlockStyleMap &instance();

    BlockStyleInfo styleForLabel(const QString &label, const QString &modelId = {}) const;

    // Merges a parsed {"default": {...}, "overrides": {...}} document over the
    // built-in table. Exposed for tests; instance() calls it with the resource.
    void applyJson(const QJsonObject &root);

    BlockStyleMap();

private:
    QHash<QString, BlockStyleInfo> m_default;
    QHash<QString, QHash<QString, BlockStyleInfo>> m_overrides;
};

inline BlockStyleInfo blockStyleForLabel(const QString &label, const QString &modelId = {})
{
    return BlockStyleMap::instance().styleForLabel(label, modelId);
}

}  // namespace llocr
