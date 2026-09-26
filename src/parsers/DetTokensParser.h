#pragma once

#include "parsers/IOutputParser.h"
#include "parsers/ParserOptions.h"

namespace llocr {

class DetTokensParser : public IOutputParser {
    Q_DISABLE_COPY_MOVE(DetTokensParser)

public:
    // Token family shared by the det-style OCR models: every region is emitted
    // as a `label [x1, y1, x2, y2]` header followed by its content, either
    // wrapped in <|det|>…<|/det|> (Unlimited-OCR) or bare, optionally prefixed
    // with `image_index=<n>` (LFM2.5-VL layout annotation). Table sub-formats
    // (OTSL tokens, verbatim <table>) are detected per block.
    explicit DetTokensParser(const ParserOptions &options = {}) : m_options(options) {}

    OcrResult parse(const QString &rawText) const override;
    QString rebuildText(const OcrPage &page) const override;
    QString id() const override;
    QString displayName() const override;

private:
    ParserOptions m_options;
};

} // namespace llocr
