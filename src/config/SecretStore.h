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
    // Why a secret could not be read or written. The kinds are told apart
    // because they need different answers: "there is no such key" is the
    // normal state of an optional key, while "the store refused" means the
    // item is there and belongs to another build of the app.
    enum class Status {
        Ok,           ///< the store answered
        NotFound,     ///< there is no such item
        Refused,      ///< the store refused: the access needs the user, or the item belongs to another build
        Unavailable,  ///< there is no usable store on this machine
        Failed,       ///< anything else
    };

    // The backend in use, for the UI to name it ("macOS Keychain", "Windows
    // Credential Manager", "Secret Service"). Empty when there is none.
    static QString backendName();

    // Whether a backend is present and usable.
    static bool isAvailable();

    // Whether the store holds an item under this name. The check looks at the
    // item's attributes, not at its value: an item the app may not read still
    // exists, and the user has to be able to delete it (ADR 233).
    static bool exists(const QString &id);

    // The value, and why it was not there. The reason is empty when the
    // store answered — including "there is no such key", which is not a
    // failure but an answer.
    static bool read(const QString &id, QString *value, QString *error = nullptr);

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
    // resolve it the same way. The error tells why there was no value: an id
    // that simply is not there stays quiet, a refusal does not.
    static bool readFirst(const QStringList &ids, QString *value, QString *error = nullptr);

    // The reason the last call failed, for the status line.
    static QString lastError();

    // The kind of the last failure. Belongs to the thread that made the
    // call, like the message.
    static Status lastStatus();

    // The secrets of the process are read once and kept in the memory: the
    // request path needs the key on every call, and a store read can block (a
    // locked keychain asks the user).
    static void forgetAllCached();
};

// The backend reports its failure here; the message is what the UI shows.
void secretStoreSetError(const QString &message);

// The platform backends, one implementation per OS. The status says what
// happened; the message is set through secretStoreSetError().
bool secretStoreBackendAvailable();
QString secretStoreBackendName();
SecretStore::Status secretStoreBackendExists(const QString &id);
SecretStore::Status secretStoreBackendRead(const QString &id, QString *value);
SecretStore::Status secretStoreBackendWrite(const QString &id, const QString &value);
SecretStore::Status secretStoreBackendRemove(const QString &id);

}  // namespace llocr
