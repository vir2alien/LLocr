#include <QRegularExpression>

#include "runtime/ServerLaunchConfig.h"

namespace llocr {

ServerLaunchConfig ServerLaunchConfig::fromSettings(const SettingsStore &s)
{
    ServerLaunchConfig cfg;
    cfg.modelPath = s.launchModelPath();
    cfg.mmprojPath = s.launchMmprojPath();
    cfg.modelAlias = s.launchModelAlias();
    cfg.host = s.launchHost();
    cfg.port = s.launchPort();
    cfg.ctxSize = s.launchCtxSize();
    cfg.gpuLayers = s.launchGpuLayers();
    cfg.threads = s.launchThreads();
    cfg.batchSize = s.launchBatchSize();
    cfg.parallel = s.launchParallel();
    cfg.flashAttn = s.launchFlashAttn();
    cfg.cacheTypeK = s.launchCacheTypeK();
    cfg.cacheTypeV = s.launchCacheTypeV();
    cfg.noMmap = s.launchNoMmap();
    cfg.jinja = s.launchJinja();
    cfg.extraArgs = parseExtraArgs(s.launchExtraArgs());
    return cfg;
}

// Splits on whitespace, honoring single and double quotes. A token wrapped in
// quotes keeps its inner spaces verbatim; a literal quote inside a double-quoted
// segment is not supported (matches the shell-like behaviour described in §4.1).
QStringList ServerLaunchConfig::parseExtraArgs(const QString &text)
{
    QStringList result;
    QString current;
    QChar quote = u'\0';
    bool hadToken = false;

    const auto push = [&]() {
        if (hadToken || !current.isEmpty()) {
            result.append(current);
            current.clear();
            hadToken = false;
        }
    };

    for (const QChar ch : text) {
        if (quote.isNull()) {
            if (ch == u'\"' || ch == u'\'') {
                if (hadToken) {
                    // "a"b — start quoting mid-token; keep what preceded it.
                    push();
                }
                quote = ch;
                hadToken = true;
            } else if (ch.isSpace()) {
                push();
            } else {
                current.append(ch);
                hadToken = true;
            }
        } else if (ch == quote) {
            push();
            quote = u'\0';
        } else {
            current.append(ch);
            hadToken = true;
        }
    }
    push();
    return result;
}

namespace {

// Quote a single token for display so it survives copy-paste into a shell.
// Wraps in double quotes and escapes embedded backslashes and quotes.
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

// Appends one flag + value pair as two argv tokens: { flag, value }.
void appendPair(QStringList &args, const QString &flag, const QString &value,
                bool appendValue = true)
{
    args.append(flag);
    if (appendValue)
        args.append(value);
}

}  // namespace

QStringList ServerLaunchConfig::toArguments(const ServerCapabilities &caps) const
{
    QStringList args;

    // --- Model (position-independent flags; always the model at minimum) ---
    auto addFlag = [&args](const QString &name) { args.append(name); };

    // --model is the one absolutely required flag for a usable server.
    if (!modelPath.isEmpty())
        appendPair(args, QStringLiteral("--model"), modelPath);

    if (!mmprojPath.isEmpty())
        appendPair(args, QStringLiteral("--mmproj"), mmprojPath);

    if (caps.supportsAlias && !modelAlias.isEmpty())
        appendPair(args, QStringLiteral("--alias"), modelAlias);

    // --- Networking ------------------------------------------------------
    if (!host.isEmpty())
        appendPair(args, QStringLiteral("--host"), host);

    if (port > 0)
        appendPair(args, QStringLiteral("--port"), QString::number(port));

    // --- Context / compute ----------------------------------------------
    if (ctxSize > 0)
        appendPair(args, QStringLiteral("--ctx-size"), QString::number(ctxSize));

    if (gpuLayers >= 0)
        appendPair(args, QStringLiteral("--n-gpu-layers"), QString::number(gpuLayers));

    if (threads > 0)
        appendPair(args, QStringLiteral("--threads"), QString::number(threads));

    if (batchSize > 0)
        appendPair(args, QStringLiteral("--batch-size"), QString::number(batchSize));

    if (parallel > 0)
        appendPair(args, QStringLiteral("--parallel"), QString::number(parallel));

    // --- Flash attention ------------------------------------------------
    if (caps.supportsFlashAttn) {
        if (caps.supportsFlashAttnValue) {
            if (!flashAttn.isEmpty())
                appendPair(args, QStringLiteral("--flash-attn"), flashAttn);
        } else if (flashAttn == QStringLiteral("on")
                   || flashAttn == QStringLiteral("1")
                   || flashAttn == QStringLiteral("true")) {
            appendPair(args, QStringLiteral("--flash-attn"), QString(), false);
        }
    }

    // --- Cache types ----------------------------------------------------
    if (caps.supportsCacheTypeK && !cacheTypeK.isEmpty())
        appendPair(args, QStringLiteral("-ctk"), cacheTypeK);
    if (caps.supportsCacheTypeV && !cacheTypeV.isEmpty())
        appendPair(args, QStringLiteral("-ctv"), cacheTypeV);

    // --- Misc -----------------------------------------------------------
    if (noMmap)
        addFlag(QStringLiteral("--no-mmap"));

    if (jinja && caps.supportsJinja)
        addFlag(QStringLiteral("--jinja"));

    // --- Extra free-form args (appended verbatim) ------------------------
    for (const QString &arg : extraArgs) {
        if (!arg.isEmpty())
            args.append(arg);
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