#include "core/ProfileRepository.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>

namespace llocr {

int ProfileRepository::loadFromDirectory(const QString& directoryPath)
{
    m_profiles.clear();
    m_errors.clear();

    QDir dir(directoryPath);
    if (!dir.exists()) {
        m_errors.append(
            QStringLiteral("Profile directory does not exist: %1").arg(directoryPath));
        return 0;
    }

    const QStringList files =
        dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);

    int loaded = 0;
    for (const QString& fileName : files) {
        const QString fullPath = dir.filePath(fileName);

        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly)) {
            m_errors.append(
                QStringLiteral("Cannot open %1: %2").arg(fileName, file.errorString()));
            continue;
        }

        const QByteArray data = file.readAll();
        file.close();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            m_errors.append(QStringLiteral("Invalid JSON in %1: %2")
                                .arg(fileName, parseError.errorString()));
            continue;
        }

        bool ok = false;
        const ModelProfile profile = ModelProfile::fromJson(doc.object(), &ok);
        if (!ok) {
            m_errors.append(
                QStringLiteral("Profile %1 is missing a required 'model_id'").arg(fileName));
            continue;
        }

        m_profiles.insert(profile.modelId, profile);
        ++loaded;
    }

    return loaded;
}

QList<ModelProfile> ProfileRepository::profiles() const
{
    QList<ModelProfile> list = m_profiles.values();
    std::sort(list.begin(), list.end(),
              [](const ModelProfile& a, const ModelProfile& b) {
                  return a.displayName.localeAwareCompare(b.displayName) < 0;
              });
    return list;
}

ModelProfile ProfileRepository::byModelId(const QString& modelId) const
{
    return m_profiles.value(modelId);
}

} // namespace llocr
