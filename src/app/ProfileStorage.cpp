#include "app/ProfileStorage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QSaveFile>

namespace llocr {

namespace ProfileStorage {

bool removeFileIfExists(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.remove()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool writeJsonAtomic(const QString &path, const QJsonObject &root,
                     QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

QJsonDocument readJson(const QString &path, bool *ok, QString *error)
{
    *ok = false;
    QFile file(path);
    if (!file.exists())
        return QJsonDocument();
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return QJsonDocument();
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = parseError.errorString();
        return QJsonDocument();
    }
    *ok = true;
    return doc;
}

}  // namespace ProfileStorage

}  // namespace llocr
