#pragma once

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>

#include "runtime/ModelCatalog.h"
#include "runtime/ModelPreset.h"
#include "runtime/ModelRegistry.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

class DownloadManager;
class RuntimeController;
class SettingsStore;

// Stage E UI controller: exposes model management (preset catalog, Hugging Face
// search/download, local registry) to QML as a third managed-runtime singleton
// (`Models`), complementing RuntimeController (server lifecycle) and
// RuntimeInstaller (llama.cpp install).
//
// Like RuntimeInstaller, network and heavy on-disk work is async: catalog /
// search / tree fetches run on QtConcurrent worker threads, file downloads
// stream on the main thread via DownloadManager. QML only sees main-thread
// state via the properties below. Settings (launch/modelPath, launch/mmprojPath,
// launch/presetId, parser/prompt/ctx) are written last, after a verified
// install (§7.2).
class ModelInstaller : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

    // Installed registry.
    Q_PROPERTY(int installedCount READ installedCount NOTIFY installedChanged)

    // Preset catalog (built-in + user, merged).
    Q_PROPERTY(int presetCount READ presetCount NOTIFY presetsChanged)

    // HF search results.
    Q_PROPERTY(int searchCount READ searchCount NOTIFY searchChanged)
    Q_PROPERTY(bool searchActive READ searchActive NOTIFY searchChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchChanged)

    // The registry entry currently selected as the launch model.
    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY installedChanged)

public:
    enum State {
        Idle = 0,
        Fetching = 1,      // fetching preset/search/tree from network or disk
        ReadyToDownload = 2,  // pending files resolved, awaiting confirm
        Downloading = 3,   // downloading model file(s)
        Error = 4,
    };
    Q_ENUM(State)

    explicit ModelInstaller(SettingsStore &settings, RuntimeController &runtime,
                            QObject *parent = nullptr);
    ~ModelInstaller() override;

    void shutdown();

    // --- accessors for properties -----------------------------------------
    int stateInt() const { return static_cast<int>(m_state); }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString statusMessage() const { return m_statusMessage; }
    int installedCount() const { return m_installed.size(); }
    int presetCount() const { return m_presets.size(); }
    int searchCount() const { return m_searchResults.size(); }
    bool searchActive() const { return m_searchActive; }
    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString &q);
    QString activeTitle() const;

    // --- QML-facing API ----------------------------------------------------
    /// Reloads the preset catalog from built-in + user catalog (mirrors rescan).
    Q_INVOKABLE void reloadPresets();

    /// Returns a QVariantMap describing an installed entry (title, path, size,
    /// quantization, origin, active, license, mmprojPath). Invalid index → empty.
    Q_INVOKABLE QVariantMap installedInfo(int index) const;
    /// Marks entry `index` as the active launch model (writes launch.* settings).
    /// Returns an error string, or empty on success.
    Q_INVOKABLE QString setActiveModel(int index);
    /// Removes a managed model (files) from disk; external only hides.
    /// Returns an error string, or empty on success.
    Q_INVOKABLE QString removeModel(int index);

    /// Rebuilds the registry index from what is on disk (after dir changes).
    Q_INVOKABLE void rescanRegistry();

    /// QVariantMap for a preset row (id, title, repo, license, ctx, vram).
    Q_INVOKABLE QVariantMap presetInfo(int index) const;
    /// Selects a preset and prepares its files for install without downloading.
    Q_INVOKABLE void preparePreset(int index);
    /// Downloads + registers + activates the currently prepared preset.
    Q_INVOKABLE void installPrepared();
    /// Downloads + registers + activates a remote search result (index).
    Q_INVOKABLE void installRemote(int index);

    /// Starts an HF search; results land in `searchCount`/`searchResult`.
    Q_INVOKABLE void startSearch();
    /// Returns a QVariantMap for a search row (id, title, license, downloads).
    Q_INVOKABLE QVariantMap searchResult(int index) const;

    /// Cancels an in-flight download.
    Q_INVOKABLE void cancelInstall();

    /// Imports a user preset catalog from a JSON file path.
    Q_INVOKABLE QString importCatalog(const QString &path);
    /// Exports the merged preset catalog to `path`.
    Q_INVOKABLE QString exportCatalog(const QString &path);
    /// Restores the default (built-in-only) preset catalog.
    Q_INVOKABLE QString resetUserCatalog();

    /// HF token, exposed read/write so QML can edit it (stored in Settings).
    Q_INVOKABLE QString hfToken() const;
    Q_INVOKABLE void setHfToken(const QString &token);

private:
    struct Pending {
        QString repo;
        QString revision;
        QString title;
        QString license;
        QString parser;
        QString prompt;
        int ctxSize = 0;
        QString presetId;
        QString dir;              // <modelsDir>/<org>__<repo>
        QString modelPath;        // absolute first part after install
        QString mmprojRel;        // file name of projector, or empty
        QStringList modelNames;   // model file names (all parts)
        QList<HfFile> files;      // full candidate file list for the repo
    };

    void setState(State next);
    void setBusy(bool busy);
    void setProgress(double p);
    void setStatusMessage(const QString &msg);

    void reloadPresetsInternal();
    void onPresetsLoadedInternal();

    void refreshInstalled();
    void beginPrepare(const ModelPreset &preset);
    void onPrepareDone(const Pending &p, const QString &err);

    void beginDownload();
    void enqueueFile(const QString &name, const QString &repo,
                     const QString &commitSha);
    void onOneDownloadFinished(bool ok);
    void emitDownloadProgress();
    void maybeFinishDownloads();
    void completeInstall();

    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    RuntimePaths m_paths;
    DownloadManager *m_downloads = nullptr;

    State m_state = State::Idle;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusMessage;

    QList<ModelPreset> m_presets;
    QList<ModelEntry> m_installed;

    QList<HfModelSummary> m_searchResults;
    QString m_searchQuery;
    bool m_searchActive = false;

    // Downloads require a stable descriptor for the pending install.
    Pending m_pending;
    int m_downloadCount = 0;
    int m_downloadDone = 0;
    bool m_downloadFailed = false;

signals:
    void stateChanged();
    void busyChanged();
    void progressChanged();
    void statusMessageChanged();
    void installedChanged();
    void presetsChanged();
    void searchChanged();
};

}  // namespace llocr