#pragma once

#include <memory>

#include <QString>
#include <QStringList>

#include "parsers/IOutputParser.h"

namespace llocr {

class ParserFactory {
public:
    static std::unique_ptr<IOutputParser> create(const QString& parserId);

    static QStringList registeredIds();
};

} // namespace llocr
