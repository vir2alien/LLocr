#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <utility>

namespace llocr {

// A user-facing message that can be re-rendered when the UI language changes.
//
// The controllers used to store the *translated* string, so switching the
// language left the status line, the error line and the file-dialog filters in
// the previous language until something else happened to overwrite them. A
// message here is either
//
//   * a literal — text produced verbatim by something that already speaks the
//     user's language (a server reply, a Pandoc error, a file path), or
//   * a translation: the context, the source key and the `%1…%n` arguments,
//     rendered on demand by text(), or
//   * a composite of the two, joined by a separator.
//
// Storing the key rather than the rendering is what makes retranslate() mean
// something: nothing is cached, so a language switch only has to re-read.
class StatusMessage
{
public:
    StatusMessage() = default;
    /// A QString is a message that is already in the user's language (a server
    /// reply, a transport error, a file path). Implicit so that a `Result` that
    /// takes a StatusMessage can still be built from a plain error string —
    /// which is what the shared transport and the adapters hand around.
    StatusMessage(const QString &text) : StatusMessage(literal(text)) {}

    /// Text that is already in the user's language, or needs no translation.
    static StatusMessage literal(const QString &text);
    /// A translatable message. `args` fills the `%1…%n` placeholders in order.
    ///
    /// Named `translate`, not `tr`, on purpose: `lupdate` reads the first string
    /// literal of a `translate(…)` call as the context, so the keys keep landing
    /// in the context the runtime looks them up in. A `StatusMessage::tr(…)`
    /// would be filed under `StatusMessage` and never match.
    static StatusMessage translate(const char *context, const QString &key,
                                   const QStringList &args = {});
    /// Convenience for the common `tr(...).arg(a).arg(b)` call sites.
    template <typename... Args>
    static StatusMessage translate(const char *context, const QString &key,
                                   Args &&...args)
    {
        StatusMessage message;
        message.m_kind = Kind::Translated;
        message.m_context = QString::fromUtf8(context);
        message.m_key = key;
        (message.m_args.append(toArg(std::forward<Args>(args))), ...);
        return message;
    }
    /// Joins messages with `separator` (a newline by default) into one line of
    /// text that is still assembled from untranslated parts.
    static StatusMessage join(QList<StatusMessage> parts,
                              const QString &separator = QStringLiteral("\n"));

    /// Appends a `%n` argument, so a translatable message reads the same as
    /// `tr(...).arg(a).arg(b)` did. Numbers go through `QString::number`, which
    /// is what `QString::arg` would have done.
    template <typename Arg, typename... Args>
    StatusMessage arg(Arg &&first, Args &&...rest) const
    {
        StatusMessage copy = *this;
        copy.m_args.append(toArg(std::forward<Arg>(first)));
        (copy.m_args.append(toArg(std::forward<Args>(rest))), ...);
        return copy;
    }

    /// Renders in the language that is installed *right now*.
    QString text() const;
    bool isEmpty() const;

    /// True when both messages would render the same text — the identity a
    /// setter compares before emitting a change notification.
    bool operator==(const StatusMessage &other) const;
    bool operator!=(const StatusMessage &other) const { return !(*this == other); }

private:
    /// The numeric overload of QString::arg, without the implicit-conversion
    /// surprises: only the types a message actually interpolates are accepted.
    static QString toArg(const QString &value) { return value; }
    static QString toArg(const char *value) { return QString::fromUtf8(value); }
    static QString toArg(int value) { return QString::number(value); }
    static QString toArg(qint64 value) { return QString::number(value); }
    static QString toArg(double value) { return QString::number(value); }
    static QString toArg(bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); }

    enum class Kind {
        Empty,
        Literal,
        Translated,
        Composite,
    };

    Kind m_kind = Kind::Empty;
    QString m_text;                        ///< Literal
    QString m_context;                     ///< Translated
    QString m_key;                         ///< Translated
    QStringList m_args;                    ///< Translated
    QList<StatusMessage> m_parts;          ///< Composite
    QString m_separator;                   ///< Composite
};

}  // namespace llocr

Q_DECLARE_METATYPE(llocr::StatusMessage)
