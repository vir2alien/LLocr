#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <QQmlEngine>

namespace llocr {

class LlamaServerProcess;
class SettingsStore;

class RuntimeLog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString serverLog READ serverLog NOTIFY serverLogChanged)

public:
    explicit RuntimeLog(SettingsStore &settings, QObject *parent = nullptr);

    QString serverLog() const;

    Q_INVOKABLE QString serverLogPath() const;
    Q_INVOKABLE QString serverLogDir() const;
    Q_INVOKABLE void copyServerLog();
    Q_INVOKABLE void clearServerLog();
    Q_INVOKABLE void openServerLogFolder();

    void setServer(LlamaServerProcess *server);

signals:
    void serverLogChanged();

private:
    SettingsStore &m_settings;
    LlamaServerProcess *m_server = nullptr;
    QTimer m_flushTimer;
};

}  // namespace llocr