#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include "core/StatusMessage.h"

namespace llocr {

// Where "something did not work" is collected, so the full text can be shown
// without being put on screen.
//
// The footer used to carry the diagnostics as wrapping labels: one DjVu page the
// decoder could not read produced a paragraph — file path, page number and the
// decoder's own complaint — under the toolbar, and the status line was pushed
// off the window. The texts did not get shorter by being wrapped, they got
// *unreadable*, so they moved here. What stayed in the footer is one elided
// line with a count and a way in (ADR 119).
//
// Entries are appended on the GUI thread and notifications are coalesced: a
// book with 600 unreadable pages must not re-render the log 600 times.
class ProblemLog : public QObject
{
    Q_OBJECT
    // See UiController.h: registered by hand in main.cpp until the module
    // singletons get their create() factories (stage 4).

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
    /// The whole log, one entry per line, rendered in the language installed
    /// right now. Nothing is cached, so a language switch is a re-read.
    QString logText() const;

    /// Appends one entry. An empty message is dropped.
    void report(const StatusMessage &message, Severity severity = Warning);

    Q_INVOKABLE void clear();
    Q_INVOKABLE void copyLog();

    /// A language switch re-renders the log (ADR 114).
    void retranslate();

signals:
    void logChanged();

private:
    struct Entry {
        Severity severity = Warning;
        StatusMessage message;
    };

    /// Bounded like the server ring buffer: a repeatedly failing import must not
    /// grow the process without limit, and the newest lines are the useful ones.
    static constexpr int kMaxEntries = 2000;

    void flush();

    QList<Entry> m_entries;
    QTimer m_flushTimer;
    int m_dropped = 0;  ///< entries fallen off the front, announced in logText()
};

}  // namespace llocr
