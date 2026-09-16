#pragma once

#include <QList>
#include <QString>
#include <QVariant>

class QJsonObject;

namespace llocr {

enum class LaunchValueKind
{
    Number,  // QVariant(double) — emitted as a decimal string
    Text,    // QVariant(QString)
    Flag     // no value — bare `--name` token
};

struct LaunchParameter
{
    QString name;
    int order = 0;
    LaunchValueKind kind = LaunchValueKind::Flag;
    QVariant value;        // unused for Flag
    QString description;   // optional UI documentation

    bool operator==(const LaunchParameter &other) const;
    bool operator!=(const LaunchParameter &other) const { return !(*this == other); }
};

struct LaunchProfile
{
    QString id;
    QString name;        // display name (falls back to id)
    QString os;          // "win" | "linux" | "macos" | "" (any)
    QString backend;     // "cuda" | "metal" | "vulkan" | "cpu" | "" (any)
    QString description;
    QList<LaunchParameter> parameters;
    static constexpr const char *kBuiltInPath = ":/profiles/serverLaunch.json";

    static const QStringList &reservedArgNames();
    static QList<LaunchProfile> parseFile(const QJsonObject &root, QString &error);

    QJsonObject toJson() const;
    const LaunchParameter *find(const QString &name) const;
    bool parametersEqual(const LaunchProfile &other) const;
    void sortByOrder();
};

}  // namespace llocr
