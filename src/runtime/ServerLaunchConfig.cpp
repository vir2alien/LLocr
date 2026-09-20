
#include "runtime/ServerLaunchConfig.h"

#include "app/LaunchProfileStore.h"
#include "app/SettingsStore.h"
#include "runtime/ServerCapabilities.h"

#include <algorithm>

namespace llocr {

ServerLaunchConfig ServerLaunchConfig::fromSettings(
    const SettingsStore &s, const LaunchProfileStore &launchProfiles,
    ConnectionRole role)
{
    ServerLaunchConfig cfg;
    if (role == ConnectionRole::Check) {
        cfg.modelPath = s.checkLaunchModelPath();
        cfg.mmprojPath = s.checkLaunchMmprojPath();
    } else {
        cfg.modelPath = s.launchModelPath();
        cfg.mmprojPath = s.launchMmprojPath();
    }
    cfg.modelAlias = s.launchModelAlias();
    cfg.host = s.launchHost();
    cfg.port = s.launchPort();
    cfg.parameters = launchProfiles.activeProfile().parameters;
    return cfg;
}

namespace {

QString displayEscape(const QString &token)
{
    if (token.isEmpty())
        return QStringLiteral("\"\"");
    bool needsQuotes = false;
    for (const QChar ch : token) {
        if (ch.isSpace() || ch == u'\"' || ch == u'\\' || ch == u'\'')
            needsQuotes = true;
    }
    if (!needsQuotes)
        return token;

    QString out = QStringLiteral("\"");
    for (const QChar ch : token) {
        if (ch == u'\"' || ch == u'\\')
            out.append(u'\\');
        out.append(ch);
    }
    out.append(u'\"');
    return out;
}

void appendPair(QStringList &args, const QString &flag, const QString &value)
{
    args.append(flag);
    args.append(value);
}

}  // namespace

QStringList ServerLaunchConfig::toArguments(const ServerCapabilities &caps) const
{
    QStringList args;

    if (!modelPath.isEmpty())
        appendPair(args, QStringLiteral("--model"), modelPath);

    if (!mmprojPath.isEmpty())
        appendPair(args, QStringLiteral("--mmproj"), mmprojPath);

    if (caps.supportsAlias && !modelAlias.isEmpty())
        appendPair(args, QStringLiteral("--alias"), modelAlias);

    if (!host.isEmpty())
        appendPair(args, QStringLiteral("--host"), host);

    if (port > 0)
        appendPair(args, QStringLiteral("--port"), QString::number(port));

    QList<LaunchParameter> rows = parameters;
    std::stable_sort(rows.begin(), rows.end(),
                     [](const LaunchParameter &a, const LaunchParameter &b) {
                         return a.order < b.order;
                     });
    for (const LaunchParameter &p : rows) {
        if (LaunchProfile::reservedArgNames().contains(p.name))
            continue;  // owned by the fields above; never duplicated
        args.append(p.name.startsWith(u'-') ? p.name : QStringLiteral("--") + p.name);
        if (p.kind == LaunchValueKind::Number)
            args.append(QString::number(p.value.toDouble()));
        else if (p.kind == LaunchValueKind::Text)
            args.append(p.value.toString());
    }

    return args;
}

QString ServerLaunchConfig::toDisplayCommand(const ServerCapabilities &caps) const
{
    QStringList tokens;
    if (!program.isEmpty())
        tokens.append(displayEscape(program));
    for (const QString &arg : toArguments(caps))
        tokens.append(displayEscape(arg));
    return tokens.join(QStringLiteral(" "));
}

}  // namespace llocr
