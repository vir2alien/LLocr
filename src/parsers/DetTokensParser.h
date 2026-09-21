#pragma once

#include "parsers/IOutputParser.h"

namespace llocr {

QString rebuildPageText(const OcrPage& page, bool keepPageNumbers = true,
                        bool tablesAsHtml = false);

class DetTokensParser : public IOutputParser {
    Q_DISABLE_COPY_MOVE(DetTokensParser)

public:
    DetTokensParser() = default;

    void setKeepPageNumbers(bool on) { m_keepPageNumbers = on; }
    bool keepPageNumbers() const { return m_keepPageNumbers; }

    void setTablesAsHtml(bool on) { m_tablesAsHtml = on; }
    bool tablesAsHtml() const { return m_tablesAsHtml; }

    OcrResult parse(const QString &rawText) const override;
    QString id() const override;

private:
    static constexpr int kBboxCoordinateRange = 1000;
    bool m_keepPageNumbers = true;
    bool m_tablesAsHtml = false;
};

} // namespace llocr