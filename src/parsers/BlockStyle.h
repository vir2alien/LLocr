#pragma once

#include <QHash>
#include <QString>

#include "core/ModelProfiles.h"

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

// Label → block style. The base vocabulary comes from ":/profiles/labels.json";
// what a model says about its own labels comes from its profile in
// ":/profiles/models", so a model with a different vocabulary is a data edit and
// not a C++ one. The built-in table below is the fallback for builds without the
// resource (unit tests) and for a malformed file.
class BlockStyleMap
{
public:
    static const BlockStyleMap &instance();

    BlockStyleInfo styleForLabel(const QString &label, const QString &modelId = {}) const;

    // Whether a label is styled at all. A model may legitimately style a label
    // as plain text, and that must not read the same as an unmapped label.
    bool knowsLabel(const QString &label, const QString &modelId = {}) const;

    // Merges the parsed {"default": {...}} document over the built-in table.
    // Exposed for tests; instance() calls it with the resource.
    void applyJson(const QJsonObject &root);

    BlockStyleMap();

private:
    QHash<QString, BlockStyleInfo> m_default;
};

inline BlockStyleInfo blockStyleForLabel(const QString &label, const QString &modelId = {})
{
    return BlockStyleMap::instance().styleForLabel(label, modelId);
}

}  // namespace llocr
