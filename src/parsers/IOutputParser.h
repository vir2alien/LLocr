#pragma once

#include <QString>

#include "core/OcrResult.h"

namespace llocr {

// Strategy that turns a raw model reply into an OcrResult. Instances are
// configured at construction time (ParserFactory::create(id, options)) and are
// stateless afterwards, so one instance can be reused for a whole run.
class IOutputParser
{
public:
    virtual ~IOutputParser() = default;

    // Parses one page reply. On success result.pages holds exactly one page.
    // Non-fatal oddities (e.g. a reply without any layout tokens) are reported
    // through OcrResult::notes instead of failing the page.
    virtual OcrResult parse(const QString &rawText) const = 0;

    // Re-renders the page Markdown after the structured fragments changed (a
    // verified FIX, a removed block, a resized image block). Parsers that do
    // not produce fragments return the page text unchanged.
    virtual QString rebuildText(const OcrPage &page) const = 0;

    virtual QString id() const = 0;

    // Human-readable name for the Settings → Output list.
    virtual QString displayName() const = 0;
};

}  // namespace llocr
