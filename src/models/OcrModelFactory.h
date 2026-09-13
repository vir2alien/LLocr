#pragma once

#include <QStringList>
#include <memory>

#include "models/OcrModel.h"

namespace llocr {

class OcrModelFactory {
public:
    static std::unique_ptr<OcrModel> create(const QString &modelId);
    static QStringList registeredIds();
    static QString defaultId();
    static QString displayNameForId(const QString &modelId);
    static QString idForDisplayName(const QString &displayName);
};

} // namespace llocr
