#include "config/SecretStore.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace llocr {

namespace {

// The error belongs to the thread that made the call: a failed read in a
// worker must not overwrite the message the UI thread is showing.
thread_local QString g_lastError;

// The secrets of the process: the values the OS store handed over, and the
// ones it refused to take. A key is read from the store once and lives here
// until the app exits.
QMutex g_cacheMutex;
QHash<QString, QString> g_cache;

}  // namespace

void secretStoreSetError(const QString &message)
{
    g_lastError = message;
}

QString SecretStore::backendName()
{
    return secretStoreBackendAvailable() ? secretStoreBackendName() : QString();
}

bool SecretStore::isAvailable()
{
    return secretStoreBackendAvailable();
}

bool SecretStore::exists(const QString &id)
{
    QString value;
    return read(id, &value);
}

bool SecretStore::read(const QString &id, QString *value)
{
    if (id.isEmpty()) {
        secretStoreSetError(QStringLiteral("The secret has no name"));
        return false;
    }

    {
        QMutexLocker locker(&g_cacheMutex);
        const auto cached = g_cache.constFind(id);
        if (cached != g_cache.constEnd()) {
            if (value)
                *value = cached.value();
            secretStoreSetError(QString());
            return !cached.value().isEmpty();
        }
    }

    QString stored;
    if (!secretStoreBackendRead(id, &stored))
        return false;

    {
        QMutexLocker locker(&g_cacheMutex);
        g_cache.insert(id, stored);
    }
    if (value)
        *value = stored;
    secretStoreSetError(QString());
    return true;
}

bool SecretStore::write(const QString &id, const QString &value)
{
    if (id.isEmpty()) {
        secretStoreSetError(QStringLiteral("The secret has no name"));
        return false;
    }

    const bool stored = secretStoreBackendWrite(id, value);

    {
        QMutexLocker locker(&g_cacheMutex);
        g_cache.insert(id, value);
    }

    if (!stored)
        return false;

    // The store has to give the value back: an item the app can write but not
    // read is not a stored key, it is a key that is lost at the next start.
    // The read goes straight to the backend — the cache would answer itself.
    QString readBack;
    if (!secretStoreBackendRead(id, &readBack) || readBack != value) {
        secretStoreSetError(QStringLiteral("The secret store took the key but did not give it back"));
        return false;
    }
    secretStoreSetError(QString());
    return true;
}

bool SecretStore::remove(const QString &id)
{
    {
        QMutexLocker locker(&g_cacheMutex);
        g_cache.remove(id);
    }
    return secretStoreBackendRemove(id);
}

bool SecretStore::readFirst(const QStringList &ids, QString *value)
{
    for (const QString &id : ids) {
        QString stored;
        if (read(id, &stored) && !stored.isEmpty()) {
            if (value)
                *value = stored;
            return true;
        }
    }
    if (value)
        value->clear();
    return false;
}

void SecretStore::forgetAllCached()
{
    QMutexLocker locker(&g_cacheMutex);
    g_cache.clear();
}

QString SecretStore::lastError()
{
    return g_lastError;
}

}  // namespace llocr
