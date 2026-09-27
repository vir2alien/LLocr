#include "core/StatusMessage.h"

#include <QCoreApplication>

namespace llocr {

StatusMessage StatusMessage::literal(const QString &text)
{
    StatusMessage message;
    if (text.isEmpty())
        return message;
    message.m_kind = Kind::Literal;
    message.m_text = text;
    return message;
}

StatusMessage StatusMessage::translate(const char *context, const QString &key, const QStringList &args)
{
    StatusMessage message;
    message.m_kind = Kind::Translated;
    message.m_context = QString::fromUtf8(context);
    message.m_key = key;
    message.m_args = args;
    return message;
}

StatusMessage StatusMessage::join(QList<StatusMessage> parts, const QString &separator)
{
    StatusMessage message;
    // A single part needs no composite: it is cheaper to keep and to render.
    if (parts.size() == 1)
        return parts.first();
    message.m_kind = Kind::Composite;
    message.m_parts = std::move(parts);
    message.m_separator = separator;
    return message;
}

QString StatusMessage::text() const
{
    switch (m_kind) {
    case Kind::Empty:
        return QString();
    case Kind::Literal:
        return m_text;
    case Kind::Composite: {
        QStringList rendered;
        rendered.reserve(m_parts.size());
        for (const StatusMessage &part : m_parts) {
            if (!part.isEmpty())
                rendered.append(part.text());
        }
        return rendered.join(m_separator);
    }
    case Kind::Translated:
        break;
    }

    // The source string is the fallback: an untranslated key must still read as
    // English rather than as an empty status line.
    QString rendered = QCoreApplication::translate(m_context.toUtf8().constData(), m_key.toUtf8().constData());
    for (const QString &arg : m_args)
        rendered = rendered.arg(arg);
    return rendered;
}

bool StatusMessage::isEmpty() const
{
    if (m_kind == Kind::Empty)
        return true;
    if (m_kind == Kind::Composite) {
        for (const StatusMessage &part : m_parts) {
            if (!part.isEmpty())
                return false;
        }
        return true;
    }
    return false;
}

bool StatusMessage::operator==(const StatusMessage &other) const
{
    return text() == other.text();
}

}  // namespace llocr
