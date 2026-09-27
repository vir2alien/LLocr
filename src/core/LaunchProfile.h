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
    Flag,    // no value — bare `--name` token
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
    static constexpr const char *kBuiltInPath = ":/profiles/serverLaunchOcr.json";

    static const QStringList &reservedArgNames();
    static QList<LaunchProfile> parseFile(const QJsonObject &root, QString &error);
    /// The built-in catalog and the user overrides file have the same shape, so
    /// both are read through this name (ADR 117).
    static QList<LaunchProfile> profilesFromJson(const QJsonObject &root, QString &error)
    {
        return parseFile(root, error);
    }
    /// Built-in defaults with the user's copy laid over them: the user's
    /// parameters win by name, the built-in keeps the descriptive fields.
    static LaunchProfile merge(const LaunchProfile &defaults,
                               const LaunchProfile &user);

    QJsonObject toJson() const;
    const LaunchParameter *find(const QString &name) const;
    bool parametersEqual(const LaunchProfile &other) const;
    bool operator==(const LaunchProfile &other) const;
    bool operator!=(const LaunchProfile &other) const { return !(*this == other); }
    void sortByOrder();

private:
    /// Parses one profile object; shared by the built-in catalog and the user
    /// overrides file, which have the same per-profile shape (ADR 117).
    static bool profileFromJson(const QJsonObject &obj, LaunchProfile &profile,
                                QString &error);
};

}  // namespace llocr
