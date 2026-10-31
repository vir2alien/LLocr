#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <QQmlEngine>

#include "runtime/LogDocumentSync.h"

class QQuickTextDocument;

namespace llocr {

class LlamaServerProcess;
class SettingsStore;

class RuntimeLog : public QObject
{
    Q_OBJECT
    // See UiController.h: registered by hand in main.cpp until the module
    // singletons get their create() factories (stage 4).

    Q_PROPERTY(int lineCount READ lineCount NOTIFY serverLogChanged)

public:
    explicit RuntimeLog(SettingsStore &settings, QObject *parent = nullptr);

    QString serverLog() const;
    int lineCount() const;

    Q_INVOKABLE QString serverLogPath() const;
    Q_INVOKABLE QString serverLogDir() const;
    Q_INVOKABLE void copyServerLog();
    Q_INVOKABLE void clearServerLog();
    Q_INVOKABLE void openServerLogFolder();
    Q_INVOKABLE void attachLogView(QQuickTextDocument *view);
    // The log window reports its visibility: while hidden the mirror stops
    // absorbing flushes, and showing it catches the document up in one
    // rebuild. The ring buffer and the log file run regardless.
    Q_INVOKABLE void setLogViewVisible(bool visible);

    void setServer(LlamaServerProcess *server);

signals:
    void serverLogChanged();

private:
    static constexpr int kMaxLines = 2000;

    // The external runtime writes no log file of its own: the view shows the
    // client's request journal instead of the server process's output.
    bool externalMode() const;
    void flushToView();

    SettingsStore &m_settings;
    LlamaServerProcess *m_server = nullptr;
    QTimer m_flushTimer;
    QStringList m_lines;
    int m_pendingDropped = 0;
    bool m_viewVisible = false;
    LogDocumentSync m_sync;
};

}  // namespace llocr
