#pragma once

#include <QString>
#include <QStringList>

#include <memory>

#include "parsers/ParserOptions.h"

namespace llocr {

class IOutputParser;

class ParserFactory
{
public:
    static const QString kAutoId;

    static QStringList selectableIds();

    static QStringList selectableDisplayNames();

    static std::unique_ptr<IOutputParser> create(const QString &parserId, const ParserOptions &options = {});

    static QStringList registeredIds();
};

}  // namespace llocr
