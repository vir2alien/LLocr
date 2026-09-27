#include "parsers/BlockStyle.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>

namespace llocr {

namespace {

const char *kBuiltInLabels[] = {
    // Unlimited-OCR (and anything else speaking the same label vocabulary).
    "title:heading",
    "image:image",
    "chart:image",
    "image_caption:italic",
    "table_caption:italic",
    "table_footnote:italic",
    "page_number:italic",
    "equation:equation",
    "table:table",
    // "text", "footer" and anything unmapped fall through to PlainText.
};

BlockStyle styleFromName(const QString &name)
{
    if (name == QLatin1String("heading"))
        return BlockStyle::Heading;
    if (name == QLatin1String("image"))
        return BlockStyle::ImagePlaceholder;
    if (name == QLatin1String("italic"))
        return BlockStyle::Italic;
    if (name == QLatin1String("equation"))
        return BlockStyle::Equation;
    if (name == QLatin1String("table"))
        return BlockStyle::Table;
    return BlockStyle::PlainText;
}

QHash<QString, BlockStyleInfo> tableFromJson(const QJsonObject &object)
{
    QHash<QString, BlockStyleInfo> table;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        BlockStyleInfo info;
        if (it.value().isString()) {
            info.style = styleFromName(it.value().toString());
        } else if (it.value().isObject()) {
            const QJsonObject entry = it.value().toObject();
            info.style = styleFromName(entry.value(QStringLiteral("style")).toString());
            info.headingLevel = entry.value(QStringLiteral("level")).toInt();
        } else {
            continue;
        }
        table.insert(it.key(), info);
    }
    return table;
}

}  // namespace

BlockStyleMap::BlockStyleMap()
{
    for (const char *entry : kBuiltInLabels) {
        const QString spec = QString::fromLatin1(entry);
        const int sep = spec.indexOf(QLatin1Char(':'));
        BlockStyleInfo info;
        info.style = styleFromName(spec.mid(sep + 1));
        m_default.insert(spec.left(sep), info);
    }
}

const BlockStyleMap &BlockStyleMap::instance()
{
    static const BlockStyleMap map = [] {
        BlockStyleMap m;
        QFile file(QStringLiteral(":/profiles/labels.json"));
        if (file.open(QIODevice::ReadOnly)) {
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error == QJsonParseError::NoError && doc.isObject())
                m.applyJson(doc.object());
        }
        return m;
    }();
    return map;
}

void BlockStyleMap::applyJson(const QJsonObject &root)
{
    const QJsonObject defaults = root.value(QStringLiteral("default")).toObject();
    if (!defaults.isEmpty())
        m_default = tableFromJson(defaults);

    const QJsonObject overrides = root.value(QStringLiteral("overrides")).toObject();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        const QHash<QString, BlockStyleInfo> table = tableFromJson(it.value().toObject());
        if (!table.isEmpty())
            m_overrides.insert(it.key(), table);
    }
}

BlockStyleInfo BlockStyleMap::styleForLabel(const QString &label, const QString &modelId) const
{
    if (!modelId.isEmpty()) {
        const auto modelIt = m_overrides.constFind(modelId);
        if (modelIt != m_overrides.constEnd()) {
            const auto styleIt = modelIt->constFind(label);
            if (styleIt != modelIt->constEnd())
                return *styleIt;
        }
    }
    return m_default.value(label, {BlockStyle::PlainText, 0});
}

}  // namespace llocr
