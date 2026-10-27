#include "config/SecretStore.h"

#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace llocr {

namespace {

// The error belongs to the thread that made the call: a failed read in a
// worker must not overwrite the message the UI thread is showing.
thread_local QString g_lastError;

// The kind of that failure, for the same reason.
thread_local SecretStore::Status g_lastStatus = SecretStore::Status::Ok;

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
    {
        QMutexLocker locker(&g_cacheMutex);
        const auto cached = g_cache.constFind(id);
        if (cached != g_cache.constEnd() && !cached.value().isEmpty())
            return true;
    }
    // The attributes, not the value: the item is there even when the app may
    // not read it, and the user has to be able to delete it.
    const Status status = secretStoreBackendExists(id);
    g_lastStatus = status;
    return status == Status::Ok;
}

bool SecretStore::read(const QString &id, QString *value, QString *error)
{
    if (id.isEmpty()) {
        secretStoreSetError(QCoreApplication::translate("SecretStore", "The secret has no name"));
        g_lastStatus = Status::Failed;
        if (error)
            *error = g_lastError;
        return false;
    }

    {
        QMutexLocker locker(&g_cacheMutex);
        const auto cached = g_cache.constFind(id);
        if (cached != g_cache.constEnd()) {
            if (value)
                *value = cached.value();
            secretStoreSetError(QString());
            g_lastStatus = cached.value().isEmpty() ? Status::NotFound : Status::Ok;
            if (error)
                error->clear();
            return !cached.value().isEmpty();
        }
    }

    QString stored;
    const Status status = secretStoreBackendRead(id, &stored);
    g_lastStatus = status;
    if (status != Status::Ok) {
        if (error)
            *error = g_lastError;
        return false;
    }

    {
        QMutexLocker locker(&g_cacheMutex);
        g_cache.insert(id, stored);
    }
    if (value)
        *value = stored;
    secretStoreSetError(QString());
    if (error)
        error->clear();
    return true;
}

bool SecretStore::write(const QString &id, const QString &value)
{
    if (id.isEmpty()) {
        secretStoreSetError(QCoreApplication::translate("SecretStore", "The secret has no name"));
        g_lastStatus = Status::Failed;
        return false;
    }

    const Status status = secretStoreBackendWrite(id, value);
    g_lastStatus = status;

    {
        QMutexLocker locker(&g_cacheMutex);
        g_cache.insert(id, value);
    }

    if (status != Status::Ok)
        return false;

    // The store has to give the value back: an item the app can write but not
    // read is not a stored key, it is a key that is lost at the next start.
    // The read goes straight to the backend — the cache would answer itself.
    QString readBack;
    const Status back = secretStoreBackendRead(id, &readBack);
    if (back != Status::Ok || readBack != value) {
        secretStoreSetError(QCoreApplication::translate("SecretStore", "The secret store took the key but did not give it back"));
        g_lastStatus = Status::Failed;
        return false;
    }
    secretStoreSetError(QString());
    g_lastStatus = Status::Ok;
    return true;
}

bool SecretStore::remove(const QString &id)
{
    {
        QMutexLocker locker(&g_cacheMutex);
        g_cache.remove(id);
    }
    const Status status = secretStoreBackendRemove(id);
    g_lastStatus = status;
    return status == Status::Ok;
}

bool SecretStore::readFirst(const QStringList &ids, QString *value, QString *error)
{
    // An id that simply is not there is the normal state of an optional key:
    // it says nothing. A refusal does — it is the reason the key is missing.
    Status failure = Status::NotFound;
    QString reason;
    for (const QString &id : ids) {
        QString stored;
        QString why;
        if (read(id, &stored, &why) && !stored.isEmpty()) {
            if (value)
                *value = stored;
            g_lastStatus = Status::Ok;
            if (error)
                error->clear();
            return true;
        }
        if (!why.isEmpty() && failure == Status::NotFound) {
            failure = g_lastStatus;
            reason = why;
        }
    }
    if (value)
        value->clear();
    g_lastStatus = failure;
    if (error)
        *error = reason;
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

SecretStore::Status SecretStore::lastStatus()
{
    return g_lastStatus;
}

}  // namespace llocr
