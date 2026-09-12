#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <QJsonValue>

class QJsonObject;

namespace llocr {

/// Value kind of a llama-server command-line parameter. CLI values are text
/// at the end, but the explicit kind keeps Number rows numeric (typo guard)
/// and Flag rows valueless; the JSON `value` type decides at parse time.
enum class LaunchValueKind
{
    Number,  // QVariant(double) — emitted as a decimal string
    Text,    // QVariant(QString)
    Flag     // no value — bare `--name` token
};

/// One parameter (argument) of the llama-server command line, stored WITHOUT
/// the leading dashes (`n-gpu-layers`, not `--n-gpu-layers`); a name that
/// already starts with `-` is passed through verbatim (short/exotic forms).
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

/// A named set of launch parameters (a llama-server preset): built-in presets
/// ship read-only in :/profiles/serverLaunch.json and may be updated with app
/// releases; the user's customized copies live in
/// <AppData>/LLocr/profiles/serverLaunch.json keyed by the preset id (full
/// copies, so user deletions stick; a preset update reaches users who have not
/// customized that preset).
struct LaunchProfile
{
    QString id;
    QString name;        // display name (falls back to id)
    QString os;          // "win" | "linux" | "macos" | "" (any)
    QString backend;     // "cuda" | "metal" | "vulkan" | "cpu" | "" (any)
    QString description;
    QList<LaunchParameter> parameters;

    static constexpr const char *kBuiltInPath = ":/profiles/serverLaunch.json";

    /// Argument names owned by the app (model/mmproj/alias/host/port come from
    /// their own settings). Rows with these names are skipped by the argv
    /// builder and rejected by the UI "add parameter" action.
    static const QStringList &reservedArgNames();

    /// Parses the whole file: `{ schemaVersion, profiles: [ … ] }` (a missing
    /// or empty `profiles` array is valid and yields an empty list; a malformed
    /// entry sets `error`). Used for both the built-in catalog and the user
    /// file.
    static QList<LaunchProfile> parseFile(const QJsonObject &root, QString &error);

    /// Serializes one profile to the built-in-style JSON object.
    QJsonObject toJson() const;

    const LaunchParameter *find(const QString &name) const;

    /// Compares parameter (name, kind, value) triples in order-sorted order;
    /// metadata and `description`s are documentation, not settings.
    bool parametersEqual(const LaunchProfile &other) const;

    void sortByOrder();
};

}  // namespace llocr
