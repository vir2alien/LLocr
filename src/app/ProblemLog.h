#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include "core/StatusMessage.h"

namespace llocr {

class ProblemLog : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(int count READ count NOTIFY logChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY logChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY logChanged)

public:
    enum Severity {
        Warning = 0,
        Error = 1,
    };
    Q_ENUM(Severity)

    explicit ProblemLog(QObject *parent = nullptr);

    int count() const { return m_entries.size(); }
    int errorCount() const;
    int warningCount() const;
    QString logText() const;

    void report(const StatusMessage &message, Severity severity = Warning);

    Q_INVOKABLE void clear();
    Q_INVOKABLE void copyLog();

    void retranslate();

signals:
    void logChanged();

private:
    struct Entry {
        Severity severity = Warning;
        StatusMessage message;
    };

    static constexpr int kMaxEntries = 2000;

    void flush();

    QList<Entry> m_entries;
    QTimer m_flushTimer;
    int m_dropped = 0;  ///< entries fallen off the front, announced in logText()
};

}  // namespace llocr
