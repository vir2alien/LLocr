#pragma once

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "config/ProfileStorage.h"

namespace llocr {

template <typename T> class ProfileStore
{
public:
    ProfileStore(QString builtInPath, QString userFileName, int schemaVersion, QString storeName, QString defaultUserId = {})
        : m_builtInPath(std::move(builtInPath)), m_userFileName(std::move(userFileName)), m_schemaVersion(schemaVersion), m_storeName(std::move(storeName)), m_defaultUserId(std::move(defaultUserId))
    {
        loadBuiltIn();
        reloadUserProfiles();
    }

    QString userPath() const { return m_userPath; }
    void setUserPath(const QString &path) { m_userPath = path; }

    const QList<T> &builtIn() const { return m_builtIn; }
    QList<T> &mutableBuiltIn() { return m_builtIn; }
    void setBuiltIn(QList<T> profiles) { m_builtIn = std::move(profiles); }
    const QHash<QString, T> &userProfiles() const { return m_userProfiles; }

    void reloadUserProfiles() { reloadUserProfilesImpl(); }

    bool hasUserProfile() const { return !m_userPath.isEmpty() && QFile::exists(m_userPath); }

    const T *findBuiltIn(const QString &id) const
    {
        for (const T &profile : m_builtIn) {
            if (profile.id == id)
                return &profile;
        }
        return nullptr;
    }

    bool isKnown(const QString &id) const { return findBuiltIn(id) != nullptr || m_userProfiles.contains(id); }

    T merged(const QString &id) const
    {
        static const T kEmpty;
        const T *defaults = findBuiltIn(id);
        const auto user = m_userProfiles.constFind(id);
        if (user == m_userProfiles.constEnd())
            return defaults ? *defaults : kEmpty;
        return defaults ? T::merge(*defaults, user.value()) : user.value();
    }

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
            qWarning("%s: cannot load built-in profiles %s: %s", qUtf8Printable(m_storeName), qUtf8Printable(m_builtInPath), qUtf8Printable(error));
        }
    }

    void reloadUserProfilesImpl()
    {
        m_userProfiles.clear();
        if (m_userPath.isEmpty() || !QFile::exists(m_userPath))
            return;
        QString error;
        bool ok = false;
        const QJsonDocument doc = ProfileStorage::readEnvelope(m_userPath, m_schemaVersion, m_storeName, &ok, &error);
        if (ok && doc.isObject()) {
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
                     qUtf8Printable(m_storeName),
                     qUtf8Printable(m_userPath),
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
                qWarning("%s: cannot remove user profiles %s: %s", qUtf8Printable(m_storeName), qUtf8Printable(m_userPath), qUtf8Printable(error));
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
            qWarning("%s: cannot write user profiles %s: %s", qUtf8Printable(m_storeName), qUtf8Printable(m_userPath), qUtf8Printable(error));
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
