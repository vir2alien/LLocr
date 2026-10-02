#pragma once

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>

#include "config/RuntimePaths.h"
#include "runtime/InstalledState.h"
#include "runtime/ModelCatalog.h"
#include "runtime/ModelPreset.h"
#include "runtime/ModelQuantModel.h"
#include "runtime/ModelRegistry.h"

namespace llocr {

struct ReconcileResult;

class InstalledState;
class ModelInstallTransaction;
class RuntimeController;
class SettingsStore;

class ModelInstaller : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

    Q_PROPERTY(int installedCount READ installedCount NOTIFY installedChanged)

    Q_PROPERTY(QObject *quantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *checkQuantModels READ checkQuantModels CONSTANT)

    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY installedChanged)
    Q_PROPERTY(QString checkActiveTitle READ checkActiveTitle NOTIFY installedChanged)

public:
    enum State {
        Idle = 0,
        Fetching = 1,
        ReadyToDownload = 2,
        Downloading = 3,
        Error = 4,
    };
    Q_ENUM(State)

    explicit ModelInstaller(SettingsStore &settings, RuntimeController &runtime, InstalledState &state, QObject *parent = nullptr);
    ~ModelInstaller() override;

    void shutdown();

    void retranslate();

    int stateInt() const { return static_cast<int>(m_state); }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString statusMessage() const { return m_statusMessage; }
    int installedCount() const { return m_installed.size(); }
    QObject *quantModels() const;
    QObject *checkQuantModels() const;
    QString activeTitle() const;
    QString checkActiveTitle() const;

    const QList<ModelPreset> &presetsForRole(bool forCheck) const { return forCheck ? m_presetsValidate : m_presets; }
    const QList<ModelEntry> &installedEntries() const { return m_installed; }

    Q_INVOKABLE void reloadPresets();
    Q_INVOKABLE QString setActiveModel(int index, bool forCheck = false);
    Q_INVOKABLE QString removeModel(int index);
    Q_INVOKABLE QString openModelFolder(int index);

    Q_INVOKABLE void refreshInstalled();
    Q_INVOKABLE void rescanRegistry();

    Q_INVOKABLE void selectQuant(const QString &key, const QString &quantId);
    Q_INVOKABLE QString useQuant(const QString &key, const QString &quantId, bool forCheck = false);
    Q_INVOKABLE void downloadQuant(const QString &key, const QString &quantId, bool forCheck = false);
    Q_INVOKABLE QString removeQuant(const QString &key, const QString &quantId, bool forCheck = false);
    Q_INVOKABLE QString removeModelRow(const QString &key, bool forCheck = false);
    Q_INVOKABLE QString openQuantFolder(const QString &key, const QString &quantId, bool forCheck = false);

    Q_INVOKABLE void installPrepared();

    Q_INVOKABLE void cancelInstall();

private:
    void setState(State next);
    void setBusy(bool busy);
    void setProgress(double p);
    void setStatusMessage(const QString &msg);

    void reloadPresetsInternal();

    void reportStaleSelections(const ReconcileResult &report);

    void syncProfileToActiveModel();

    ModelQuantModel *quantModel(bool forCheck) const;
    int installedIndexFor(const QString &key, const QString &quantId, bool forCheck) const;
    void publishInstalled();

    SettingsStore &m_settings;
    InstalledState &m_installState;
    RuntimeController &m_runtime;
    ModelInstallTransaction *m_transaction = nullptr;

    State m_state = State::Idle;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusMessage;

    QList<ModelPreset> m_presets;
    QList<ModelPreset> m_presetsValidate;
    QList<ModelEntry> m_installed;
    ModelQuantModel *m_ocrModels = nullptr;
    ModelQuantModel *m_checkModels = nullptr;

signals:
    void stateChanged();
    void busyChanged();
    void progressChanged();
    void statusMessageChanged();
    void installedChanged();
    void presetsChanged();
};

}  // namespace llocr