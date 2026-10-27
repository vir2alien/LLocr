#include "core/RequestJournal.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace llocr {

namespace {

QString sizeText(int bytes)
{
    if (bytes <= 0)
        return QStringLiteral("0 B");
    if (bytes < 1024)
        return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KiB");
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MiB");
}

QString errorText(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return QString();
    const QJsonValue error = doc.object().value(QStringLiteral("error"));
    if (error.isObject())
        return error.toObject().value(QStringLiteral("message")).toString();
    if (error.isString())
        return error.toString();
    return QString();
}

}  // namespace

RequestJournal &RequestJournal::instance()
{
    static RequestJournal journal;
    return journal;
}

void RequestJournal::record(const Record &record)
{
    {
        QMutexLocker locker(&m_mutex);
        m_records.append(record);
        while (m_records.size() > kMaxRecords)
            m_records.removeFirst();
    }
    emit changed();
}

QStringList RequestJournal::lines() const
{
    QMutexLocker locker(&m_mutex);
    QStringList out;
    for (const Record &record : m_records)
        out += format(record);
    return out;
}

int RequestJournal::count() const
{
    QMutexLocker locker(&m_mutex);
    return m_records.size();
}

void RequestJournal::clear()
{
    {
        QMutexLocker locker(&m_mutex);
        m_records.clear();
    }
    emit changed();
}

QString RequestJournal::excerptOf(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
            const QString content = message.value(QStringLiteral("content")).toString();
            if (!content.isEmpty())
                return content;
        }
        const QString error = errorText(body);
        if (!error.isEmpty())
            return error;
    }
    return QString::fromUtf8(body).simplified();
}

QStringList RequestJournal::format(const Record &record)
{
    QStringList out;
    out.append(record.at.toString(QStringLiteral("HH:mm:ss.zzz")) + QStringLiteral("  POST ") + record.endpoint);
    QString meta = QStringLiteral("request ") + sizeText(record.requestBytes);
    if (!record.model.isEmpty())
        meta += QStringLiteral(" · model ") + record.model;
    if (record.status > 0)
        meta += QStringLiteral(" · HTTP ") + QString::number(record.status);
    meta += QStringLiteral(" · reply ") + sizeText(record.responseBytes);
    meta += QStringLiteral(" · ") + QString::number(record.durationMs / 1000.0, 'f', 1) + QStringLiteral(" s");
    out.append(QStringLiteral("    ") + meta);
    if (!record.error.isEmpty())
        out.append(QStringLiteral("    error: ") + record.error);
    if (!record.excerpt.isEmpty()) {
        QString excerpt = record.excerpt;
        if (excerpt.size() > kExcerptLimit)
            excerpt = excerpt.left(kExcerptLimit) + QStringLiteral("…");
        excerpt.replace(QLatin1Char('\n'), QStringLiteral(" "));
        out.append(QStringLiteral("    reply: ") + excerpt);
    }
    return out;
}

}  // namespace llocr
