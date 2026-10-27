#include "config/SecretStore.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QHash>
#include <QVariantMap>

// The Linux Secret Service API over D-Bus (org.freedesktop.secrets): the
// store GNOME's keyring and KWallet expose. No libsecret and no second
// dependency — the protocol is spoken directly over the session bus.
//
// The item is a generic password: the "service" attribute is the app's, the
// "account" attribute is the secret's id. A locked collection answers with a
// prompt path that only a user can resolve, so that case is an error, not a
// silent empty value.

namespace llocr {

namespace {

constexpr const char *kServiceName = "org.freedesktop.secrets";
constexpr const char *kObjectPath = "/org/freedesktop/secrets";
constexpr const char *kServiceInterface = "org.freedesktop.Secret.Service";
constexpr const char *kItemInterface = "org.freedesktop.Secret.Item";
constexpr const char *kLabelProperty = "org.freedesktop.Secret.Item.Label";
constexpr const char *kAttributesProperty = "org.freedesktop.Secret.Item.Attributes";
constexpr const char *kAppService = "org.llocr.app";

// The store's messages are the user's: they say what to do. They live in one
// translation context, shared by the three backends.
QString storeTr(const char *text)
{
    return QCoreApplication::translate("SecretStore", text);
}

// The secret struct of the protocol: (session, parameters, value, type).
struct SecretPayload {
    QDBusObjectPath session;
    QHash<QString, QVariant> parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &arg, const SecretPayload &payload)
{
    arg.beginStructure();
    arg << payload.session << payload.parameters << payload.value << payload.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, SecretPayload &payload)
{
    arg.beginStructure();
    arg >> payload.session >> payload.parameters >> payload.value >> payload.contentType;
    arg.endStructure();
    return arg;
}

QDBusConnection sessionBus()
{
    return QDBusConnection::sessionBus();
}

bool serviceRegistered()
{
    QDBusConnection bus = sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusInterface *iface = bus.interface();
    return iface && iface->isServiceRegistered(QLatin1String(kServiceName));
}

// The session carries the (plain) key exchange; one session serves the whole
// process.
QDBusObjectPath openSession()
{
    static const QDBusObjectPath session = []() {
        QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kServiceInterface), QStringLiteral("OpenSession"));
        message << QStringLiteral("plain") << QVariant::fromValue(QByteArray());
        const QDBusMessage reply = sessionBus().call(message);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return QDBusObjectPath();
        return qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
    }();
    return session;
}

QList<QDBusObjectPath> asPaths(const QVariant &value)
{
    return value.value<QList<QDBusObjectPath>>();
}

// Unlocks the items and returns the ones that are ready. A prompt path means
// a locked collection: only the user can open it, so the read fails.
bool unlockItems(const QList<QDBusObjectPath> &items)
{
    if (items.isEmpty())
        return true;

    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kServiceInterface), QStringLiteral("Unlock"));
    message << QVariant::fromValue(items);
    const QDBusMessage reply = sessionBus().call(message);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        secretStoreSetError(storeTr("Secret Service: the collection could not be unlocked"));
        return false;
    }
    const QList<QVariant> args = reply.arguments();
    if (args.size() < 2 || !qdbus_cast<QDBusObjectPath>(args.at(1)).path().isEmpty()) {
        secretStoreSetError(storeTr("The keyring is locked — unlock it and try again"));
        return false;
    }
    return true;
}

}  // namespace

bool secretStoreBackendAvailable()
{
    static const bool available = serviceRegistered();
    return available;
}

QString secretStoreBackendName()
{
    return QStringLiteral("Secret Service");
}

SecretStore::Status secretStoreBackendExists(const QString &id)
{
    if (!secretStoreBackendAvailable())
        return SecretStore::Status::Unavailable;

    // The search answers with the paths of the items; the value stays sealed.
    QDBusMessage search = QDBusMessage::createMethodCall(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kServiceInterface), QStringLiteral("SearchItems"));
    QHash<QString, QVariant> attributes;
    attributes.insert(QStringLiteral("service"), QString::fromLatin1(kAppService));
    attributes.insert(QStringLiteral("account"), id);
    search << attributes;
    const QDBusMessage searchReply = sessionBus().call(search);
    if (searchReply.type() != QDBusMessage::ReplyMessage || searchReply.arguments().size() < 2) {
        secretStoreSetError(storeTr("Secret Service: the search failed"));
        return SecretStore::Status::Failed;
    }
    if (asPaths(searchReply.arguments().at(0)).isEmpty() && asPaths(searchReply.arguments().at(1)).isEmpty()) {
        secretStoreSetError(storeTr("Secret Service: there is no such key"));
        return SecretStore::Status::NotFound;
    }
    return SecretStore::Status::Ok;
}

SecretStore::Status secretStoreBackendRead(const QString &id, QString *value)
{
    if (!secretStoreBackendAvailable()) {
        secretStoreSetError(storeTr("No secret service on the session bus"));
        return SecretStore::Status::Unavailable;
    }
    const QDBusObjectPath session = openSession();
    if (session.path().isEmpty()) {
        secretStoreSetError(storeTr("Secret Service: the session could not be opened"));
        return SecretStore::Status::Failed;
    }

    QDBusMessage search = QDBusMessage::createMethodCall(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kServiceInterface), QStringLiteral("SearchItems"));
    QHash<QString, QString> attributes;
    attributes.insert(QStringLiteral("service"), QString::fromLatin1(kAppService));
    attributes.insert(QStringLiteral("account"), id);
    search << attributes;
    const QDBusMessage searchReply = sessionBus().call(search);
    if (searchReply.type() != QDBusMessage::ReplyMessage || searchReply.arguments().size() < 2) {
        secretStoreSetError(storeTr("Secret Service: the search failed"));
        return SecretStore::Status::Failed;
    }

    const QList<QDBusObjectPath> unlocked = asPaths(searchReply.arguments().at(0));
    const QList<QDBusObjectPath> locked = asPaths(searchReply.arguments().at(1));
    if (unlocked.isEmpty() && locked.isEmpty()) {
        secretStoreSetError(storeTr("Secret Service: there is no such key"));
        return SecretStore::Status::NotFound;
    }
    if (!unlockItems(locked)) {
        // A locked keyring is not a missing key: the key is there, and only
        // the user can open it.
        return SecretStore::Status::Refused;
    }

    for (const QDBusObjectPath &path : unlocked) {
        QDBusMessage get = QDBusMessage::createMethodCall(QLatin1String(kServiceName), path.path(), QLatin1String(kItemInterface), QStringLiteral("GetSecret"));
        get << QVariant::fromValue(session);
        const QDBusMessage reply = sessionBus().call(get);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            continue;
        const SecretPayload payload = reply.arguments().at(0).value<SecretPayload>();
        if (value)
            *value = QString::fromUtf8(payload.value);
        return SecretStore::Status::Ok;
    }

    secretStoreSetError(storeTr("Secret Service: the key could not be read"));
    return SecretStore::Status::Failed;
}

SecretStore::Status secretStoreBackendWrite(const QString &id, const QString &value)
{
    if (!secretStoreBackendAvailable()) {
        secretStoreSetError(storeTr("No secret service on the session bus"));
        return SecretStore::Status::Unavailable;
    }
    const QDBusObjectPath session = openSession();
    if (session.path().isEmpty()) {
        secretStoreSetError(storeTr("Secret Service: the session could not be opened"));
        return SecretStore::Status::Failed;
    }

    QVariantMap properties;
    properties.insert(QString::fromLatin1(kLabelProperty), id);
    QHash<QString, QString> itemAttributes;
    itemAttributes.insert(QStringLiteral("service"), QString::fromLatin1(kAppService));
    itemAttributes.insert(QStringLiteral("account"), id);
    properties.insert(QString::fromLatin1(kAttributesProperty), QVariant::fromValue(itemAttributes));

    SecretPayload payload;
    payload.session = session;
    payload.value = value.toUtf8();
    payload.contentType = QStringLiteral("text/plain");

    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kServiceInterface), QStringLiteral("CreateItem"));
    message << properties << QVariant::fromValue(payload) << true;
    const QDBusMessage reply = sessionBus().call(message);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        secretStoreSetError(storeTr("Secret Service: the key could not be stored"));
        return SecretStore::Status::Failed;
    }
    return SecretStore::Status::Ok;
}

SecretStore::Status secretStoreBackendRemove(const QString &id)
{
    if (!secretStoreBackendAvailable()) {
        secretStoreSetError(storeTr("No secret service on the session bus"));
        return SecretStore::Status::Unavailable;
    }

    QDBusMessage search = QDBusMessage::createMethodCall(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kServiceInterface), QStringLiteral("SearchItems"));
    QHash<QString, QString> attributes;
    attributes.insert(QStringLiteral("service"), QString::fromLatin1(kAppService));
    attributes.insert(QStringLiteral("account"), id);
    search << attributes;
    const QDBusMessage searchReply = sessionBus().call(search);
    if (searchReply.type() != QDBusMessage::ReplyMessage || searchReply.arguments().size() < 2)
        return SecretStore::Status::Failed;

    const QList<QDBusObjectPath> items = asPaths(searchReply.arguments().at(0)) + asPaths(searchReply.arguments().at(1));
    for (const QDBusObjectPath &path : items) {
        QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(kServiceName), path.path(), QLatin1String(kItemInterface), QStringLiteral("Delete"));
        const QDBusMessage reply = sessionBus().call(message);
        if (reply.type() != QDBusMessage::ReplyMessage) {
            secretStoreSetError(storeTr("Secret Service: the key could not be deleted"));
            return SecretStore::Status::Failed;
        }
    }
    return SecretStore::Status::Ok;
}

}  // namespace llocr
