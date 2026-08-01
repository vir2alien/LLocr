#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/ModelProfile.h"

namespace llocr {

/**
 * @brief Loads and holds model profiles from a directory of JSON files.
 *
 * Each *.json file in the directory is parsed into a ModelProfile
 */
class ProfileRepository {
public:
    int loadFromDirectory(const QString& directoryPath);

    QList<ModelProfile> profiles() const;
    ModelProfile byModelId(const QString& modelId) const;
    bool isEmpty() const { return m_profiles.isEmpty(); }
    QStringList errors() const { return m_errors; }

private:
    QHash<QString, ModelProfile> m_profiles;
    QStringList m_errors;
};

} // namespace llocr
