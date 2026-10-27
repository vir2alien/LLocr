#pragma once

#include <QString>
#include <QStringList>

namespace llocr {

// The OS secret store: the macOS Keychain, the Windows Credential Manager and
// the Linux Secret Service (ADR 227). One interface, three backends; a secret
// is named by a string id.
//
// There is no encrypted-file fallback: when the OS store is missing the key
// lives in the process memory until the app exits, and the UI says exactly
// that. A hand-rolled encrypted file would look like protection without being
// any — the same refusal as "no digest, no install" (ADR 212).
class SecretStore
{
public:
    // The backend in use, for the UI to name it ("macOS Keychain", "Windows
    // Credential Manager", "Secret Service"). Empty when there is none.
    static QString backendName();

    // Whether a backend is present and usable.
    static bool isAvailable();

    static bool exists(const QString &id);
    static bool read(const QString &id, QString *value);

    // Stores the secret. The value is kept in the process memory either way;
    // the result says whether the OS store took it — false means the key
    // lives in the memory until the app exits, and the caller must say so.
    //
    // A store that took the value but will not give it back has not stored
    // it: the write is read back and compared, and a mismatch is a failure.
    static bool write(const QString &id, const QString &value);

    static bool remove(const QString &id);

    // The value of the first id that holds one. The rule "the task's own key,
    // or the shared one" lives here, so the settings and the request path
    // resolve it the same way.
    static bool readFirst(const QStringList &ids, QString *value);

    // The reason the last call failed, for the status line.
    static QString lastError();

    // The secrets of the process are read once and kept in the memory: the
    // request path needs the key on every call, and a store read can block (a
    // locked keychain asks the user).
    static void forgetAllCached();
};

// The backend reports its failure here; the message is what the UI shows.
void secretStoreSetError(const QString &message);

// The platform backends, one implementation per OS.
bool secretStoreBackendAvailable();
QString secretStoreBackendName();
bool secretStoreBackendRead(const QString &id, QString *value);
bool secretStoreBackendWrite(const QString &id, const QString &value);
bool secretStoreBackendRemove(const QString &id);

}  // namespace llocr
