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
    StatusMessage(const QString &text) : StatusMessage(literal(text)) {}

    static StatusMessage literal(const QString &text);
    static StatusMessage translate(const char *context, const QString &key, const QStringList &args = {});
    template <typename... Args> static StatusMessage translate(const char *context, const QString &key, Args &&...args)
    {
        StatusMessage message;
        message.m_kind = Kind::Translated;
        message.m_context = QString::fromUtf8(context);
        message.m_key = key;
        (message.m_args.append(toArg(std::forward<Args>(args))), ...);
        return message;
    }
    static StatusMessage join(QList<StatusMessage> parts, const QString &separator = QStringLiteral("\n"));

    template <typename Arg, typename... Args> StatusMessage arg(Arg &&first, Args &&...rest) const
    {
        StatusMessage copy = *this;
        copy.m_args.append(toArg(std::forward<Arg>(first)));
        (copy.m_args.append(toArg(std::forward<Args>(rest))), ...);
        return copy;
    }

    QString text() const;
    bool isEmpty() const;

    bool operator==(const StatusMessage &other) const;
    bool operator!=(const StatusMessage &other) const { return !(*this == other); }

private:
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
    QString m_text;                ///< Literal
    QString m_context;             ///< Translated
    QString m_key;                 ///< Translated
    QStringList m_args;            ///< Translated
    QList<StatusMessage> m_parts;  ///< Composite
    QString m_separator;           ///< Composite
};

}  // namespace llocr

Q_DECLARE_METATYPE(llocr::StatusMessage)
