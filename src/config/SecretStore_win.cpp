#include "config/SecretStore.h"

#include <QByteArray>
#include <QCoreApplication>

// wincred.h must stay after windows.h: it does not include windows.h itself,
// and the include sort would flip the two.
// clang-format off
#include <windows.h>
#include <wincred.h>
// clang-format on

// The Windows Credential Manager: a generic credential per secret, named
// "LLocr:<id>". CRED_PERSIST_LOCAL_MACHINE keeps it across restarts for the
// same user account — the credential belongs to the user, not to the session.

namespace llocr {

namespace {

QString targetName(const QString &id)
{
    return QStringLiteral("LLocr:") + id;
}

const wchar_t kUserName[] = L"LLocr";

// The store's messages are the user's: they say what to do. They live in one
// translation context, shared by the three backends.
QString storeTr(const char *text)
{
    return QCoreApplication::translate("SecretStore", text);
}

}  // namespace

bool secretStoreBackendAvailable()
{
    return true;
}

QString secretStoreBackendName()
{
    return QStringLiteral("Windows Credential Manager");
}

SecretStore::Status secretStoreBackendExists(const QString &id)
{
    // The credential manager keeps no access control of its own for the
    // current user: the read is the check.
    QString value;
    return secretStoreBackendRead(id, &value);
}

SecretStore::Status secretStoreBackendRead(const QString &id, QString *value)
{
    const QString target = targetName(id);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) {
            secretStoreSetError(storeTr("Credential Manager: there is no such key"));
            return SecretStore::Status::NotFound;
        }
        secretStoreSetError(storeTr("Credential Manager: error %1").arg(code));
        return SecretStore::Status::Failed;
    }
    const QByteArray blob(reinterpret_cast<const char *>(credential->CredentialBlob), int(credential->CredentialBlobSize));
    CredFree(credential);
    if (value)
        *value = QString::fromUtf8(blob);
    return SecretStore::Status::Ok;
}

SecretStore::Status secretStoreBackendWrite(const QString &id, const QString &value)
{
    const QString target = targetName(id);
    QByteArray blob = value.toUtf8();

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(kUserName);

    if (!CredWriteW(&credential, 0)) {
        secretStoreSetError(storeTr("Credential Manager: error %1").arg(GetLastError()));
        return SecretStore::Status::Failed;
    }
    return SecretStore::Status::Ok;
}

SecretStore::Status secretStoreBackendRemove(const QString &id)
{
    const QString target = targetName(id);
    if (!CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0)) {
        const DWORD code = GetLastError();
        if (code != ERROR_NOT_FOUND) {
            secretStoreSetError(storeTr("Credential Manager: error %1").arg(code));
            return SecretStore::Status::Failed;
        }
    }
    return SecretStore::Status::Ok;
}

}  // namespace llocr
