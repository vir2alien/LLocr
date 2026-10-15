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
    // The sequential queue of the task window (stage 6): active from
    // queueTaskInstalls() until the last queued install settles.
    Q_PROPERTY(bool taskQueueActive READ taskQueueActive NOTIFY taskQueueStateChanged)
    Q_PROPERTY(int taskQueueRemaining READ taskQueueRemaining NOTIFY taskQueueStateChanged)

    Q_PROPERTY(int installedCount READ installedCount NOTIFY installedChanged)

    // The unified management list (the rework plan's stage 7): every profile
    // and every installed entry, nothing role-filtered, nothing active.
    Q_PROPERTY(QObject *allModels READ allModels CONSTANT)

    Q_PROPERTY(QObject *quantModels READ quantModels CONSTANT)
    Q_PROPERTY(QObject *checkQuantModels READ checkQuantModels CONSTANT)
    Q_PROPERTY(QObject *decisionQuantModels READ decisionQuantModels CONSTANT)
    Q_PROPERTY(QObject *layoutQuantModels READ layoutQuantModels CONSTANT)
    Q_PROPERTY(QObject *rawPageOcrQuantModels READ rawPageOcrQuantModels CONSTANT)
    Q_PROPERTY(QObject *fixQuantModels READ fixQuantModels CONSTANT)

public:
    enum State : int {
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
    QObject *allModels() const;
    QObject *quantModels() const;
    QObject *checkQuantModels() const;
    QObject *decisionQuantModels() const;
    QObject *layoutQuantModels() const;
    QObject *rawPageOcrQuantModels() const;
    QObject *fixQuantModels() const;

    // task is a task id: "pageOcr", "rawPageOcr", "blockRecognition",
    // "decision", "layout" or "fix".
    const QList<ModelPreset> &presetsForRole(const QString &task) const;
    const QList<ModelEntry> &installedEntries() const { return m_installed; }

    Q_INVOKABLE void reloadPresets();
    Q_INVOKABLE QString setActiveModel(int index, const QString &task = QStringLiteral("pageOcr"));
    Q_INVOKABLE QString removeModel(int index);
    Q_INVOKABLE QString openModelFolder(int index);

    Q_INVOKABLE void refreshInstalled();
    Q_INVOKABLE void rescanRegistry();

    Q_INVOKABLE void selectQuant(const QString &key, const QString &quantId);
    // task is a task id ("pageOcr", …) — the downloaded model is assigned to
    // it on completion — or empty for a plain download with no assignment
    // (the management window; assignment lives in the task window).
    Q_INVOKABLE void downloadQuant(const QString &key, const QString &quantId, const QString &task = QString());
    Q_INVOKABLE QString removeQuant(const QString &key, const QString &quantId, const QString &task = QStringLiteral("pageOcr"));
    Q_INVOKABLE QString removeModelRow(const QString &key, const QString &task = QStringLiteral("pageOcr"));
    Q_INVOKABLE QString openQuantFolder(const QString &key, const QString &quantId, const QString &task = QStringLiteral("pageOcr"));

    Q_INVOKABLE void installPrepared();

    Q_INVOKABLE void cancelInstall();

    // The task window's save flow (the rework plan's stage 6). choices is a
    // list of {task, key, quantId}: installed quants are activated for their
    // task, missing ones are returned as {task, model, quant, size} for the
    // download dialog — with the profile choice recorded so it survives.
    Q_INVOKABLE QVariantList applyTaskChoices(const QVariantList &choices);
    // Sequential download queue over the missing choices: one install at a
    // time (the shared transaction), each result assigned to its task on
    // completion.
    Q_INVOKABLE void queueTaskInstalls(const QVariantList &choices);
    Q_INVOKABLE void cancelTaskInstalls();
    bool taskQueueActive() const { return m_taskQueueActive; }
    int taskQueueRemaining() const { return m_taskQueue.size(); }

private:
    void setState(State next);
    void setBusy(bool busy);
    void setProgress(double p);
    void setStatusMessage(const QString &msg);
    void setTaskQueueActive(bool active);
    void startNextQueuedInstall();

    struct TaskInstall {
        QString task;
        QString key;
        QString quantId;
    };

    void reloadPresetsInternal();

    void reportStaleSelections(const ReconcileResult &report);

    void syncProfileToActiveModel();

    ModelQuantModel *quantModel(const QString &task) const;
    bool isActiveForAnyTask(const ModelEntry &entry) const;
    int installedIndexFor(const QString &key, const QString &quantId, const QString &role) const;
    void publishInstalled();

    SettingsStore &m_settings;
    InstalledState &m_installState;
    RuntimeController &m_runtime;
    ModelInstallTransaction *m_transaction = nullptr;

    State m_state = State::Idle;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusMessage;

    QList<ModelPreset> m_presetsAll;
    QList<ModelPreset> m_presets;
    QList<ModelPreset> m_presetsValidate;
    QList<ModelPreset> m_presetsDecision;
    QList<ModelPreset> m_presetsLayout;
    QList<ModelPreset> m_presetsRawPageOcr;
    QList<ModelPreset> m_presetsFix;
    QList<ModelEntry> m_installed;
    ModelQuantModel *m_allModels = nullptr;
    ModelQuantModel *m_pageOcrModels = nullptr;
    ModelQuantModel *m_checkModels = nullptr;
    ModelQuantModel *m_decisionModels = nullptr;
    ModelQuantModel *m_layoutModels = nullptr;
    ModelQuantModel *m_rawPageOcrModels = nullptr;
    ModelQuantModel *m_fixModels = nullptr;
    QList<TaskInstall> m_taskQueue;
    bool m_taskQueueActive = false;

signals:
    void stateChanged();
    void busyChanged();
    void progressChanged();
    void statusMessageChanged();
    void installedChanged();
    void presetsChanged();
    void taskQueueStateChanged();
};

}  // namespace llocr