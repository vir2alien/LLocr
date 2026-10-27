#include "config/SecretStore.h"

#include <QByteArray>

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
QString describeWriteFailure(OSStatus status)
{
    switch (status) {
    case errSecAuthFailed:
        return QStringLiteral("Keychain: the access was denied — the item belongs to an earlier build of the app. Remove it in Keychain Access, or allow the access.");
    case errSecInteractionNotAllowed:
        return QStringLiteral("Keychain: the access needs the user's confirmation, and none was given.");
    case errSecDuplicateItem:
        return QStringLiteral("Keychain: the item is still there and cannot be replaced.");
    default:
        return QStringLiteral("Keychain: %1").arg(describeStatus(status));
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

bool secretStoreBackendRead(const QString &id, QString *value)
{
    if (!secretStoreBackendAvailable()) {
        secretStoreSetError(QStringLiteral("The macOS Keychain is not available"));
        return false;
    }

    CFDictionaryRef query = queryFor(id);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess) {
        if (status == errSecItemNotFound)
            secretStoreSetError(QStringLiteral("The Keychain has no such key"));
        else if (status == errSecAuthFailed)
            secretStoreSetError(describeWriteFailure(status));
        else
            secretStoreSetError(QStringLiteral("Keychain: %1").arg(describeStatus(status)));
        return false;
    }
    if (!result || CFGetTypeID(result) != CFDataGetTypeID()) {
        if (result)
            CFRelease(result);
        secretStoreSetError(QStringLiteral("The Keychain item is not a password"));
        return false;
    }

    const CFDataRef data = static_cast<CFDataRef>(result);
    const QByteArray blob(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), int(CFDataGetLength(data)));
    CFRelease(result);
    if (value)
        *value = QString::fromUtf8(blob);
    return true;
}

bool secretStoreBackendWrite(const QString &id, const QString &value)
{
    if (!secretStoreBackendAvailable()) {
        secretStoreSetError(QStringLiteral("The macOS Keychain is not available"));
        return false;
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
        secretStoreSetError(describeWriteFailure(status));
        return false;
    }
    return true;
}

bool secretStoreBackendRemove(const QString &id)
{
    if (!secretStoreBackendAvailable()) {
        secretStoreSetError(QStringLiteral("The macOS Keychain is not available"));
        return false;
    }

    CFDictionaryRef query = queryFor(id);
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        secretStoreSetError(QStringLiteral("Keychain: %1").arg(describeStatus(status)));
        return false;
    }
    return true;
}

}  // namespace llocr
