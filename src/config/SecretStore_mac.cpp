#include "config/SecretStore.h"

#include <QByteArray>
#include <QCoreApplication>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

// The macOS Keychain. The CoreFoundation C API is used on purpose: no
// Objective-C objects, no memory-management modes, one less thing to get
// wrong in a file that holds a secret.
//
// The item is a generic password in the login keychain: the service name is
// the app's, the account is the secret's id. The keychain's own ACL is the
// protection — the item is readable by the application that created it and
// not by the others — so no accessibility attribute is set (on macOS it would
// be ignored for the login keychain anyway).

namespace llocr {

namespace {

constexpr const char *kServiceName = "org.llocr.app";

// A name that is never stored: reading it proves the keychain answers.
constexpr const char *kProbeId = "org.llocr.app/probe";

// The store's messages are the user's: they say what to do. They live in one
// translation context, shared by the three backends.
QString storeTr(const char *text)
{
    return QCoreApplication::translate("SecretStore", text);
}

CFStringRef copyString(const QByteArray &utf8)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, utf8.constData(), kCFStringEncodingUTF8);
}

CFDictionaryRef queryFor(const QString &id)
{
    CFStringRef service = copyString(QByteArray(kServiceName));
    CFStringRef account = copyString(id.toUtf8());
    const void *keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
    const void *values[] = {kSecClassGenericPassword, service, account};
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(service);
    CFRelease(account);
    return query;
}

QString describeStatus(OSStatus status)
{
    CFStringRef message = SecCopyErrorMessageString(status, nullptr);
    if (!message)
        return QString::number(static_cast<int>(status));
    const QString text = QString::fromCFString(message);
    CFRelease(message);
    return text;
}

// The Keychain binds an item to the code that created it, and the code of an
// ad-hoc signed build is its hash: a rebuild cannot touch the old item, and
// the Keychain asks the user for permission. These are the answers the user
// needs, not the bare status.
QString describeRefusal(OSStatus status, const QString &id)
{
    switch (status) {
    case errSecAuthFailed:
        return storeTr("Keychain: the access was denied — the item “%1” belongs to an earlier build of the app. "
                       "Answer the Keychain prompt with “Always Allow”, or delete the item in Keychain Access "
                       "(service “%2”, account “%1”) and enter the key again.")
            .arg(id, QString::fromLatin1(kServiceName));
    case errSecInteractionNotAllowed:
        return storeTr("Keychain: the access to the item “%1” needs the user's confirmation, and none was given. "
                       "Allow it, or delete the item in Keychain Access (service “%2”, account “%1”) and enter the key again.")
            .arg(id, QString::fromLatin1(kServiceName));
    case errSecDuplicateItem:
        return storeTr("Keychain: the item “%1” is still there and cannot be replaced.").arg(id);
    default:
        return storeTr("Keychain: %1").arg(describeStatus(status));
    }
}

SecretStore::Status statusFor(OSStatus status)
{
    switch (status) {
    case errSecSuccess:
        return SecretStore::Status::Ok;
    case errSecItemNotFound:
        return SecretStore::Status::NotFound;
    case errSecAuthFailed:
    case errSecInteractionNotAllowed:
    case errSecDuplicateItem:
        return SecretStore::Status::Refused;
    default:
        return SecretStore::Status::Failed;
    }
}

}  // namespace

bool secretStoreBackendAvailable()
{
    static const bool available = []() {
        CFDictionaryRef query = queryFor(QString::fromLatin1(kProbeId));
        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        if (result)
            CFRelease(result);
        // "No such item" means the keychain answered: the store is there.
        return status == errSecSuccess || status == errSecItemNotFound;
    }();
    return available;
}

QString secretStoreBackendName()
{
    return QStringLiteral("macOS Keychain");
}

SecretStore::Status secretStoreBackendExists(const QString &id)
{
    if (!secretStoreBackendAvailable())
        return SecretStore::Status::Unavailable;

    // The attributes of an item can be read without the permission for its
    // value: the item is there even when the app may not read it. That is
    // what lets the user delete a key another build has left behind.
    CFDictionaryRef query = queryFor(id);
    const void *keys[] = {kSecReturnAttributes, kSecMatchLimit};
    const void *values[] = {kCFBooleanTrue, kSecMatchLimitOne};
    CFDictionaryRef match = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(query);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(match, &result);
    CFRelease(match);
    if (result)
        CFRelease(result);
    return statusFor(status);
}

SecretStore::Status secretStoreBackendRead(const QString &id, QString *value)
{
    if (!secretStoreBackendAvailable()) {
        secretStoreSetError(storeTr("The macOS Keychain is not available"));
        return SecretStore::Status::Unavailable;
    }

    CFDictionaryRef query = queryFor(id);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess) {
        if (status == errSecItemNotFound)
            secretStoreSetError(storeTr("The Keychain has no such key"));
        else
            secretStoreSetError(describeRefusal(status, id));
        return statusFor(status);
    }
    if (!result || CFGetTypeID(result) != CFDataGetTypeID()) {
        if (result)
            CFRelease(result);
        secretStoreSetError(storeTr("The Keychain item is not a password"));
        return SecretStore::Status::Failed;
    }

    const CFDataRef data = static_cast<CFDataRef>(result);
    const QByteArray blob(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), int(CFDataGetLength(data)));
    CFRelease(result);
    if (value)
        *value = QString::fromUtf8(blob);
    return SecretStore::Status::Ok;
}

SecretStore::Status secretStoreBackendWrite(const QString &id, const QString &value)
{
    if (!secretStoreBackendAvailable()) {
        secretStoreSetError(storeTr("The macOS Keychain is not available"));
        return SecretStore::Status::Unavailable;
    }

    const QByteArray blob = value.toUtf8();

    CFStringRef service = copyString(QByteArray(kServiceName));
    CFStringRef account = copyString(id.toUtf8());
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(blob.constData()), CFIndex(blob.size()));
    const void *queryKeys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
    const void *queryValues[] = {kSecClassGenericPassword, service, account};
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, queryKeys, queryValues, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    // An existing item is updated in place. The delete-then-add pair loses
    // the old item when the add fails — and the delete needs the same access
    // as the update, so it fails exactly when the update would.
    const void *updateKeys[] = {kSecValueData};
    const void *updateValues[] = {data};
    CFDictionaryRef update = CFDictionaryCreate(kCFAllocatorDefault, updateKeys, updateValues, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    OSStatus status = SecItemUpdate(query, update);
    CFRelease(update);

    if (status == errSecItemNotFound) {
        // There is no item yet: add it.
        const void *addKeys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData};
        const void *addValues[] = {kSecClassGenericPassword, service, account, data};
        CFDictionaryRef item = CFDictionaryCreate(kCFAllocatorDefault, addKeys, addValues, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        status = SecItemAdd(item, nullptr);
        CFRelease(item);
    }

    CFRelease(query);
    CFRelease(data);
    CFRelease(service);
    CFRelease(account);

    if (status != errSecSuccess) {
        secretStoreSetError(describeRefusal(status, id));
        return statusFor(status);
    }
    return SecretStore::Status::Ok;
}

SecretStore::Status secretStoreBackendRemove(const QString &id)
{
    if (!secretStoreBackendAvailable()) {
        secretStoreSetError(storeTr("The macOS Keychain is not available"));
        return SecretStore::Status::Unavailable;
    }

    CFDictionaryRef query = queryFor(id);
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        secretStoreSetError(describeRefusal(status, id));
        return statusFor(status);
    }
    return SecretStore::Status::Ok;
}

}  // namespace llocr
