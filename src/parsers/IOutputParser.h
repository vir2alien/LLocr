#pragma once

#include <QList>
#include <QString>

#include "core/OcrResult.h"

namespace llocr {

class IOutputParser
{
public:
    virtual ~IOutputParser() = default;

    virtual OcrResult parse(const QString &rawText) const = 0;
    virtual QString rebuildText(const OcrPage &page) const = 0;

    struct RebuiltPageText {
        QString text;
        QList<BlockTextRange> ranges;
    };
    virtual RebuiltPageText rebuildTextWithRanges(const OcrPage &page) const { return {rebuildText(page), {}}; }

    virtual QString id() const = 0;

    virtual QString displayName() const = 0;
};

}  // namespace llocr
