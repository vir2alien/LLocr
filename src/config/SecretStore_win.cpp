#include "config/SecretStore.h"

#include <QByteArray>

#include <wincred.h>
#include <windows.h>

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

}  // namespace

bool secretStoreBackendAvailable()
{
    return true;
}

QString secretStoreBackendName()
{
    return QStringLiteral("Windows Credential Manager");
}

bool secretStoreBackendRead(const QString &id, QString *value)
{
    const QString target = targetName(id);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential)) {
        secretStoreSetError(QStringLiteral("Credential Manager: error %1").arg(GetLastError()));
        return false;
    }
    const QByteArray blob(reinterpret_cast<const char *>(credential->CredentialBlob), int(credential->CredentialBlobSize));
    CredFree(credential);
    if (value)
        *value = QString::fromUtf8(blob);
    return true;
}

bool secretStoreBackendWrite(const QString &id, const QString &value)
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
        secretStoreSetError(QStringLiteral("Credential Manager: error %1").arg(GetLastError()));
        return false;
    }
    return true;
}

bool secretStoreBackendRemove(const QString &id)
{
    const QString target = targetName(id);
    if (!CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0)) {
        const DWORD code = GetLastError();
        if (code != ERROR_NOT_FOUND) {
            secretStoreSetError(QStringLiteral("Credential Manager: error %1").arg(code));
            return false;
        }
    }
    return true;
}

}  // namespace llocr
