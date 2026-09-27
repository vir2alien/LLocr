#pragma once

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "app/ProfileStorage.h"

namespace llocr {

/// The lifecycle every *profile* store shares: a built-in list shipped as a
/// resource, a user file of overrides keyed by profile id, a merge of the two on
/// read, and a save that writes only what differs (deleting the file when
/// nothing does).
///
/// `LaunchProfileStore` and `RequestProfileStore` were two copies of this, and
/// they had drifted: the launch and request files carried a `schemaVersion` that
/// nothing ever checked, so a file written by a newer build was read as if it
/// were current. ADR 84 rejected a *base class* because the domain logic
/// differs; the domain logic is what stayed out, this is only the envelope.
///
/// `T` must provide: `QString id`, `operator==`, `QJsonObject toJson() const`,
/// `static QList<T> profilesFromJson(const QJsonObject &, QString &error)` and
/// `static T merge(const T &defaults, const T &user)`.
template <typename T>
class ProfileStore
{
public:
    ProfileStore(QString builtInPath, QString userFileName, int schemaVersion,
                 QString storeName, QString defaultUserId = {})
        : m_builtInPath(std::move(builtInPath))
        , m_userFileName(std::move(userFileName))
        , m_schemaVersion(schemaVersion)
        , m_storeName(std::move(storeName))
        , m_defaultUserId(std::move(defaultUserId))
    {
        loadBuiltIn();
        reloadUserProfiles();
    }

    /// The path of the user overrides; empty until `setUserPath()` is called
    /// (the store needs the runtime directories, which the owner supplies).
    QString userPath() const { return m_userPath; }
    void setUserPath(const QString &path) { m_userPath = path; }

    const QList<T> &builtIn() const { return m_builtIn; }
    QList<T> &mutableBuiltIn() { return m_builtIn; }
    const QHash<QString, T> &userProfiles() const { return m_userProfiles; }

    /// Re-reads the user overrides. The path comes from the settings, so the
    /// owner calls this once it has set it.
    void reloadUserProfiles() { reloadUserProfilesImpl(); }

    bool hasUserProfile() const { return !m_userPath.isEmpty() && QFile::exists(m_userPath); }

    /// The built-in profile with this id, or null.
    const T *findBuiltIn(const QString &id) const
    {
        for (const T &profile : m_builtIn) {
            if (profile.id == id)
                return &profile;
        }
        return nullptr;
    }

    bool isKnown(const QString &id) const
    {
        return findBuiltIn(id) != nullptr || m_userProfiles.contains(id);
    }

    /// Built-in defaults with the user's copy laid over them — or the defaults
    /// verbatim when there is no copy. The distinction matters: "no user copy"
    /// and "a user copy with no parameters" mean different things, and only the
    /// type's merge can tell them apart.
    T merged(const QString &id) const
    {
        static const T kEmpty;
        const T *defaults = findBuiltIn(id);
        const auto user = m_userProfiles.constFind(id);
        if (user == m_userProfiles.constEnd())
            return defaults ? *defaults : kEmpty;
        return defaults ? T::merge(*defaults, user.value()) : user.value();
    }

    /// Stores (or drops) the user's copy of `profile`, writing the file only when
    /// it differs from the built-in. Returns true when something changed.
    bool putUserProfile(const T &profile)
    {
        if (profile.id.isEmpty())
            return false;
        const T *defaults = findBuiltIn(profile.id);
        if (defaults && profile == *defaults) {
            if (!m_userProfiles.contains(profile.id))
                return false;
            m_userProfiles.remove(profile.id);
        } else {
            if (m_userProfiles.value(profile.id) == profile && m_userProfiles.contains(profile.id))
                return false;
            m_userProfiles.insert(profile.id, profile);
        }
        persistUserProfiles();
        return true;
    }

    bool removeUserProfile(const QString &id)
    {
        if (!m_userProfiles.remove(id))
            return false;
        persistUserProfiles();
        return true;
    }

    /// Drops every user override (the user file goes away entirely).
    void resetToBuiltIn()
    {
        if (m_userProfiles.isEmpty())
            return;
        m_userProfiles.clear();
        persistUserProfiles();
    }

private:
    void loadBuiltIn()
    {
        m_builtIn.clear();
        QString error;
        QFile builtIn(m_builtInPath);
        if (builtIn.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(builtIn.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                error = parseError.errorString();
            } else {
                m_builtIn = T::profilesFromJson(doc.object(), error);
            }
        } else {
            error = builtIn.errorString();
        }
        if (!error.isEmpty()) {
            qWarning("%s: cannot load built-in profiles %s: %s",
                     qUtf8Printable(m_storeName), qUtf8Printable(m_builtInPath),
                     qUtf8Printable(error));
        }
    }

    void reloadUserProfilesImpl()
    {
        m_userProfiles.clear();
        if (m_userPath.isEmpty() || !QFile::exists(m_userPath))
            return;
        QString error;
        bool ok = false;
        const QJsonDocument doc = ProfileStorage::readEnvelope(
            m_userPath, m_schemaVersion, m_storeName, &ok, &error);
        if (ok && doc.isObject()) {
            // The whole document goes through the type's own parser: a legacy
            // file can have a different shape than the built-in catalog, and
            // only the type knows which shapes it has to accept.
            for (const T &profile : T::profilesFromJson(doc.object(), error)) {
                T copy = profile;
                if (copy.id.isEmpty())
                    copy.id = m_defaultUserId;
                if (copy.id.isEmpty())
                    continue;  // an id is the key; without one it cannot be saved
                m_userProfiles.insert(copy.id, copy);
            }
        } else if (!ok) {
            error = QStringLiteral("cannot read the file");
        } else if (!doc.isObject()) {
            error = QStringLiteral("not a JSON object");
        }
        if (!error.isEmpty()) {
            qWarning("%s: cannot load user profiles %s: %s "
                     "(falling back to the built-in profiles)",
                     qUtf8Printable(m_storeName), qUtf8Printable(m_userPath),
                     qUtf8Printable(error));
        }
    }

    void persistUserProfiles()
    {
        if (m_userPath.isEmpty())
            return;
        if (m_userProfiles.isEmpty()) {
            QString error;
            if (!ProfileStorage::removeFileIfExists(m_userPath, &error)) {
                qWarning("%s: cannot remove user profiles %s: %s",
                         qUtf8Printable(m_storeName), qUtf8Printable(m_userPath),
                         qUtf8Printable(error));
            }
            return;
        }
        QJsonObject body;
        QJsonArray profiles;
        for (const T &profile : m_userProfiles)
            profiles.append(profile.toJson());
        body.insert(QLatin1String(kProfilesKey), profiles);

        QString error;
        if (!ProfileStorage::writeEnvelope(m_userPath, m_schemaVersion, body, &error)) {
            qWarning("%s: cannot write user profiles %s: %s",
                     qUtf8Printable(m_storeName), qUtf8Printable(m_userPath),
                     qUtf8Printable(error));
        }
    }

    static constexpr const char *kProfilesKey = "profiles";

    QString m_builtInPath;
    QString m_userFileName;
    int m_schemaVersion = 1;
    QString m_storeName;
    QString m_defaultUserId;
    QString m_userPath;
    QList<T> m_builtIn;
    QHash<QString, T> m_userProfiles;
};

}  // namespace llocr
