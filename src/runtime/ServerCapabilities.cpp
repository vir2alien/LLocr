#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTextStream>

#include "runtime/ServerCapabilities.h"

namespace llocr {

// ---------------------------------------------------------------------------
// Build-number parsing (tolerant, §5.3 step 2)
// ---------------------------------------------------------------------------

int ServerCapabilities::extractBuildNumber(const QString &versionOutput)
{
    // Several tolerant regexes; fall back to "unknown" (-1) when none hit
    // (§5 Stage B task 1: “версия неизвестна, но бинарник отвечает”).

    // 1) "build: 4536 (b4536)", "build b999", "build = 12000".
    static const QRegularExpression buildWord(
        QStringLiteral(R"(\bbuild[ :=]+\s*(?:b)?(\d{2,6})\b)"),
        QRegularExpression::CaseInsensitiveOption);
    // 2) bare b-tag: "b10594", "b3999". \b anchors the leading b at a word
    //    boundary, so a run at the start of the string matches (a digit must
    //    follow).
    static const QRegularExpression bTag(QStringLiteral(R"(\bb(\d{3,6})\b)"),
                                         QRegularExpression::CaseInsensitiveOption);
    // 3) "version: 4536" (some builds print this).
    static const QRegularExpression versionWord(
        QStringLiteral(R"(\b(?:version|release)[ :=]+\s*(\d{3,6})\b)"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch m = buildWord.match(versionOutput);
    if (!m.hasMatch())
        m = bTag.match(versionOutput);
    if (!m.hasMatch())
        m = versionWord.match(versionOutput);
    if (!m.hasMatch())
        return -1;
    return m.captured(1).toInt();
}

// ---------------------------------------------------------------------------
// Detection (allowlist by build range, refined by --help text). §5.3.
// ---------------------------------------------------------------------------

ServerCapabilities ServerCapabilities::detect(const QString &versionOutput,
                                              const QString &helpOutput)
{
    ServerCapabilities caps;
    caps.versionText = versionOutput.trimmed();

    // "the binary answers" is the validity criterion (not the file name).
    caps.ok = !caps.versionText.isEmpty() || !helpOutput.trimmed().isEmpty();

    const int build = extractBuildNumber(versionOutput);
    if (build > 0) {
        caps.build = QStringLiteral("b%1").arg(build);
        caps.belowMinimum = build < kMinimumBuildNumber;
    }

    // --- Allowlist by build range (ADR 41 step 2) ------------------------
    // only applied when a build number was parsed; conservative defaults when
    // unknown (the --help pass below may still enable flags it proves).
    if (build > 0) {
        // --flash-attn predates b4000 (bare boolean). Value form appeared
        // around the Flash-Attention rework (on|off|auto).
        caps.supportsFlashAttn = true;
        caps.supportsFlashAttnValue = (build >= 5000);
        caps.supportsAlias = (build >= 5000);
        caps.supportsJinja = (build >= 4500);
        caps.supportsCacheTypeK = (build >= 5400);
        caps.supportsCacheTypeV = (build >= 5400);
    }

    // --- --help refinement (step 3): enable/disable each flag. -----------
    if (!helpOutput.isEmpty()) {
        const bool hasFlash = helpOutput.contains(QStringLiteral("--flash-attn"));
        caps.supportsFlashAttn = caps.supportsFlashAttn && hasFlash;
        if (hasFlash) {
            // on|off|auto (or [on|off]) text ⇒ the binary accepts a value.
            caps.supportsFlashAttnValue =
                helpOutput.contains(QStringLiteral("on|off|auto"));
        }
        caps.supportsAlias = caps.supportsAlias
                             && helpOutput.contains(QStringLiteral("--alias"));
        caps.supportsJinja = caps.supportsJinja
                             && helpOutput.contains(QStringLiteral("--jinja"));
        caps.supportsCacheTypeK = caps.supportsCacheTypeK
                                  && helpOutput.contains(QStringLiteral("-ctk"));
        caps.supportsCacheTypeV = caps.supportsCacheTypeV
                                  && helpOutput.contains(QStringLiteral("-ctv"));
    }

    return caps;
}

// ---------------------------------------------------------------------------
// JSON (cache) — §7.2 requires schema-versioned JSON and atomic writes.
// '1' bytes are stored as booleans to keep the file small and editable.
// ---------------------------------------------------------------------------

QJsonObject ServerCapabilities::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("schemaVersion"), 1);
    o.insert(QStringLiteral("versionText"), versionText);
    o.insert(QStringLiteral("build"), build);
    o.insert(QStringLiteral("ok"), ok);
    o.insert(QStringLiteral("belowMinimum"), belowMinimum);
    o.insert(QStringLiteral("flashAttn"), supportsFlashAttn);
    o.insert(QStringLiteral("flashAttnValue"), supportsFlashAttnValue);
    o.insert(QStringLiteral("alias"), supportsAlias);
    o.insert(QStringLiteral("jinja"), supportsJinja);
    o.insert(QStringLiteral("ctk"), supportsCacheTypeK);
    o.insert(QStringLiteral("ctv"), supportsCacheTypeV);
    return o;
}

ServerCapabilities ServerCapabilities::fromJson(const QJsonObject &o)
{
    ServerCapabilities caps;
    caps.versionText = o.value(QStringLiteral("versionText")).toString();
    caps.build = o.value(QStringLiteral("build")).toString();
    caps.ok = o.value(QStringLiteral("ok")).toBool();
    caps.belowMinimum = o.value(QStringLiteral("belowMinimum")).toBool();
    caps.supportsFlashAttn = o.value(QStringLiteral("flashAttn")).toBool(true);
    caps.supportsFlashAttnValue = o.value(QStringLiteral("flashAttnValue")).toBool();
    caps.supportsAlias = o.value(QStringLiteral("alias")).toBool(true);
    caps.supportsJinja = o.value(QStringLiteral("jinja")).toBool();
    caps.supportsCacheTypeK = o.value(QStringLiteral("ctk")).toBool();
    caps.supportsCacheTypeV = o.value(QStringLiteral("ctv")).toBool();
    return caps;
}

QString ServerCapabilities::cacheFileName(const QString &cacheDir,
                                          const QString &binaryPath)
{
    const QFileInfo fi(binaryPath);
    const QString key = QStringLiteral("%1@%2")
                            .arg(fi.absoluteFilePath(), QString::number(fi.lastModified().toSecsSinceEpoch()));
    const QByteArray hash =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(cacheDir).filePath(QStringLiteral("capabilities-%1.json").arg(
        QString::fromLatin1(hash)));
}

}  // namespace llocr