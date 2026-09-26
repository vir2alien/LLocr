#pragma once

#include <QString>
#include <QStringList>

#include <memory>

#include "parsers/ParserOptions.h"

namespace llocr {

class IOutputParser;

class ParserFactory {
public:
    // Settings → Output value meaning "use the parser the selected OCR model
    // adapter declares" (OcrModel::defaultParserId()). Resolved by
    // AppController::effectiveParserId(); never reaches create().
    static const QString kAutoId;

    // "auto" followed by every real parser id, in Settings order.
    static QStringList selectableIds();

    // Display names matching selectableIds() one-to-one.
    static QStringList selectableDisplayNames();

    static std::unique_ptr<IOutputParser> create(const QString &parserId,
                                                 const ParserOptions &options = {});

    // Ids that resolve to a parser (everything except kAutoId).
    static QStringList registeredIds();
};

} // namespace llocr
