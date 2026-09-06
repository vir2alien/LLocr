#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <QtConcurrent>

#include "app/SettingsStore.h"
#include "runtime/DownloadManager.h"
#include "runtime/ModelInstaller.h"
#include "runtime/ModelPresetCatalog.h"
#include "runtime/RuntimeController.h"

namespace llocr {

namespace {

QString repoDirName(const QString &repo)
{
    QStringList parts;
    for (const QString &seg : repo.split(QLatin1Char('/'))) {
        QString s = seg.trimmed();
        if (s.isEmpty() || s == QLatin1String(".") || s == QLatin1String(".."))
            continue;
        s.replace(QLatin1Char('\\'), QStringLiteral("_"));
        static const QRegularExpression hostile(
            QStringLiteral("[^A-Za-z0-9._-]"));
        s.replace(hostile, QStringLiteral("_"));
        if (!s.isEmpty())
            parts.append(s);
    }
    if (parts.isEmpty())
        return QStringLiteral("model");
    return parts.join(QStringLiteral("__"));
}

void selectModelFiles(const QList<HfFile> &tree, const QString &prefer,
                      const QString &preferMmproj, QStringList *modelPaths,
                      QString &mmprojRel)
{
    modelPaths->clear();
    mmprojRel.clear();

    QStringList singles;                                // repo-relative paths
    QHash<QString, QPair<QStringList, qint64>> groups;  // key -> (paths,size)
    QStringList projectors;
    QHash<QString, qint64> sizeByPath;                  // path -> size

    for (const HfFile &f : tree) {
        if (f.isDir)
            continue;
        const ModelFileKind kind = ModelCatalog::fileKind(f.name);
        if (kind == ModelFileKind::NotModel)
            continue;
        if (kind == ModelFileKind::Vision) {
            projectors.append(f.path);
            continue;
        }
        sizeByPath.insert(f.path, f.size);
        QString base;
        int idx = 0, cnt = 0;
        if (ModelCatalog::splitMultiPart(f.name, &base, &idx, &cnt)) {
            const QString key = base + QStringLiteral("::") + QString::number(cnt);
            groups[key].first.append(f.path);
            groups[key].second += f.size;
        } else {
            singles.append(f.path);
        }
    }
    for (auto it = groups.begin(); it != groups.end(); ++it)
        std::sort(it.value().first.begin(), it.value().first.end(),
                  &ModelCatalog::splitAscending);

    if (mmprojRel.isEmpty()) {
        for (const QString &p : projectors) {
            if (!preferMmproj.isEmpty()
                && ModelCatalog::leafName(p) == preferMmproj) {
                mmprojRel = p;
                break;
            }
        }
        if (mmprojRel.isEmpty() && !projectors.isEmpty())
            mmprojRel = projectors.first();
    }

    if (!prefer.isEmpty()) {
        QString base;
        int idx = 0, cnt = 0;
        if (ModelCatalog::splitMultiPart(prefer, &base, &idx, &cnt)) {
            const QString key = base + QStringLiteral("::") + QString::number(cnt);
            if (groups.contains(key))
                *modelPaths = groups.value(key).first;
        } else {
            for (const QString &p : singles) {
                if (ModelCatalog::leafName(p) == prefer) {
                    *modelPaths = {p};
                    break;
                }
            }
        }
        if (!modelPaths->isEmpty())
            return;
    }

    QString largestSingle;
    qint64 largestSize = -1;
    for (const QString &p : singles) {
        const qint64 sz = sizeByPath.value(p);
        if (sz > largestSize) {
            largestSize = sz;
            largestSingle = p;
        }
    }

    QString largestGroup;
    qint64 groupBest = -1;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (it.value().second > groupBest) {
            groupBest = it.value().second;
            largestGroup = it.key();
        }
    }

    if (largestSingle.isEmpty() && largestGroup.isEmpty())
        return;

    if (groupBest > largestSize)
        *modelPaths = groups.value(largestGroup).first;
    else
        *modelPaths = {largestSingle};
}

}  // namespace

ModelInstaller::ModelInstaller(SettingsStore &settings, RuntimeController &runtime,
                               QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_runtime(runtime)
    , m_paths(settings.runtimeRootDir(), settings.runtimeModelsDir())
    , m_downloads(new DownloadManager(this))
{
    // Live progress: repaint the bar as data arrives, not only when a
    // file finishes (§ Stage E task 2).
    connect(m_downloads, &DownloadManager::progressChanged, this,
            [this]() { emitDownloadProgress(); });

    reloadPresetsInternal();
    refreshInstalled();
}

ModelInstaller::~ModelInstaller()
{
    if (m_downloads)
        m_downloads->cancelAll(true);
}

void ModelInstaller::shutdown()
{
    if (m_downloads)
        m_downloads->cancelAll(true);
}

void ModelInstaller::setState(State next)
{
    if (m_state == next)
        return;
    m_state = next;
    emit stateChanged();
}

void ModelInstaller::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ModelInstaller::setProgress(double p)
{
    if (qFuzzyCompare(m_progress, p) || p < 0.0 || p > 1.0)
        return;
    m_progress = p;
    emit progressChanged();
}

void ModelInstaller::setStatusMessage(const QString &msg)
{
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void ModelInstaller::setSearchQuery(const QString &q)
{
    if (m_searchQuery == q)
        return;
    m_searchQuery = q;
    emit searchChanged();
}

QString ModelInstaller::activeTitle() const
{
    const QString active = m_settings.launchModelPath();
    for (const ModelEntry &e : m_installed) {
        if (!e.modelPath.isEmpty() && e.modelPath == active)
            return e.title;
    }
    return QString();
}

void ModelInstaller::reloadPresets()
{
    reloadPresetsInternal();
}

void ModelInstaller::reloadPresetsInternal()
{
    QString err;
    const QString userPath =
        QDir(m_paths.modelsDir()).filePath(QStringLiteral("catalog.json"));
    m_presets = ModelPresetCatalog::load(userPath, err);
    if (!err.isEmpty())
        setStatusMessage(err);
    emit presetsChanged();
}

void ModelInstaller::refreshInstalled()
{
    QString err;
    bool rebuilt = false;
    m_installed = ModelRegistry::load(m_paths.modelsDir(), rebuilt, err);
    if (!err.isEmpty() && !rebuilt)
        setStatusMessage(err);
    emit installedChanged();
}

void ModelInstaller::rescanRegistry()
{
    QString err;
    m_installed = ModelRegistry::scanModelsDir(m_paths.modelsDir());
    ModelRegistry::save(m_paths.modelsDir(), m_installed, err);
    refreshInstalled();
}

QVariantMap ModelInstaller::installedInfo(int index) const
{
    QVariantMap out;
    if (index < 0 || index >= m_installed.size())
        return out;
    const ModelEntry &e = m_installed.at(index);

    // Display name derived from the model file, e.g. "Unlimited-OCR-Q8_0.gguf"
    // → "Unlimited-OCR Q8_0". The stored title is the repo id for search
    // installs and is not user friendly.
    QString display = QFileInfo(e.modelPath).completeBaseName();
    const QString q = e.quantization.trimmed();
    if (!q.isEmpty() && display.endsWith(QLatin1Char('-') + q))
        display.chop(q.size() + 1);
    if (display.isEmpty())
        display = e.title.isEmpty() ? e.repo : e.title;
    if (!q.isEmpty())
        display += QLatin1Char(' ') + q;

    out.insert(QStringLiteral("title"), display);
    out.insert(QStringLiteral("path"), e.modelPath);
    out.insert(QStringLiteral("mmprojPath"), e.mmprojPath);
    out.insert(QStringLiteral("size"), QVariant::fromValue(e.byteSize));
    out.insert(QStringLiteral("quantization"), e.quantization);
    out.insert(QStringLiteral("origin"),
               e.origin == ModelOrigin::Managed ? QStringLiteral("managed")
                                                : QStringLiteral("external"));
    out.insert(QStringLiteral("license"), e.license);
    out.insert(QStringLiteral("repo"), e.repo);
    out.insert(QStringLiteral("active"),
               !e.modelPath.isEmpty() && e.modelPath == m_settings.launchModelPath());
    out.insert(QStringLiteral("parts"), e.parts.size());
    return out;
}

QString ModelInstaller::setActiveModel(int index)
{
    if (index < 0 || index >= m_installed.size())
        return tr("Invalid model selection");
    const ModelEntry &e = m_installed.at(index);
    if (e.modelPath.isEmpty())
        return tr("This model has no model file selected");

    m_settings.setLaunchModelPath(e.modelPath);
    if (!e.mmprojPath.isEmpty())
        m_settings.setLaunchMmprojPath(e.mmprojPath);
    if (!e.parser.isEmpty())
        m_settings.setParserId(e.parser);
    if (e.ctxSize > 0)
        m_settings.setLaunchCtxSize(e.ctxSize);
    m_settings.forceSave();
    emit installedChanged();
    return QString();
}

QString ModelInstaller::removeModel(int index)
{
    if (index < 0 || index >= m_installed.size())
        return tr("Invalid model selection");
    const ModelEntry &e = m_installed.at(index);
    const bool active = !e.modelPath.isEmpty()
                        && e.modelPath == m_settings.launchModelPath();
    const bool ready = m_runtime.state() == RuntimeState::Ready;
    const QString guard =
        ModelRegistry::removalError(e, m_paths.modelsDir(), active, ready);
    if (!guard.isEmpty())
        return guard;

    QDir d(e.dir);

    // A repo directory may be shared by several quants (multi-quant installs
    // with a common mmproj). Only delete the files this entry owns; the shared
    // mmproj is removed only when no other entry in the same directory uses it.
    bool sharedDir = false;
    for (const ModelEntry &x : m_installed) {
        if (&x == &e)
            continue;
        if (QFileInfo(x.dir).canonicalFilePath()
            == QFileInfo(e.dir).canonicalFilePath()) {
            sharedDir = true;
            break;
        }
    }

    if (sharedDir) {
        QStringList owned = e.parts;
        owned.prepend(e.modelPath);
        bool mmprojShared = false;
        if (!e.mmprojPath.isEmpty()) {
            for (const ModelEntry &x : m_installed) {
                if (&x == &e)
                    continue;
                if (QFileInfo(x.dir).canonicalFilePath()
                        == QFileInfo(e.dir).canonicalFilePath()
                    && x.mmprojPath == e.mmprojPath) {
                    mmprojShared = true;
                    break;
                }
            }
            if (!mmprojShared)
                owned << e.mmprojPath;
        }
        for (const QString &path : std::as_const(owned)) {
            QFile f(path);
            if (f.exists() && !f.remove())
                return tr("Unable to remove model file: %1").arg(path);
        }
        if (d.isEmpty())
            QDir().rmdir(e.dir);
    } else if (!d.isEmpty()) {
        if (!d.removeRecursively())
            return tr("Unable to remove model directory: %1").arg(e.dir);
    }

    QList<ModelEntry> updated = m_installed;
    updated.removeIf([&](const ModelEntry &x) { return x.id == e.id; });
    QString saveErr;
    if (!ModelRegistry::save(m_paths.modelsDir(), updated, saveErr)) {
        refreshInstalled();
        return tr("Model files removed, but the registry could not be saved: %1")
                   .arg(saveErr);
    }
    m_installed = updated;
    emit installedChanged();
    return QString();
}

// True when the file the preset installs (same repo + same model file name) is
// already present in the registry, so the Install button can be disabled.
bool ModelInstaller::isPresetInstalled(const ModelPreset &p) const
{
    const QString modelLeaf = ModelCatalog::leafName(p.model);
    for (const ModelEntry &e : std::as_const(m_installed)) {
        if (e.repo != p.repo)
            continue;
        if (!e.modelPath.isEmpty()
            && ModelCatalog::leafName(e.modelPath) == modelLeaf)
            return true;
    }
    return false;
}

QVariantMap ModelInstaller::presetInfo(int index) const
{
    QVariantMap out;
    if (index < 0 || index >= m_presets.size())
        return out;
    const ModelPreset &p = m_presets.at(index);
    out.insert(QStringLiteral("id"), p.id);
    out.insert(QStringLiteral("title"), p.title);
    out.insert(QStringLiteral("repo"), p.repo);
    out.insert(QStringLiteral("license"), p.license);
    out.insert(QStringLiteral("ctxSize"), p.ctxSize);
    out.insert(QStringLiteral("minBuild"), p.minBuild);
    out.insert(QStringLiteral("approxVramGb"), p.approxVramGb);
    out.insert(QStringLiteral("installed"), isPresetInstalled(p));
    return out;
}

void ModelInstaller::preparePreset(int index)
{
    if (m_busy)
        return;
    if (index < 0 || index >= m_presets.size()) {
        setStatusMessage(tr("No preset selected"));
        setState(State::Error);
        return;
    }
    beginPrepare(m_presets.at(index));
}

void ModelInstaller::beginPrepare(const ModelPreset &preset)
{
    setBusy(true);
    setState(State::Fetching);
    setStatusMessage(tr("Looking up %1 …").arg(preset.repo));

    const QString repo = preset.repo;
    const QString pin = preset.revision;
    const QString prefer = preset.model;
    const QString preferMmproj = preset.mmproj;

    const QString token = m_settings.hfToken();
    const QString modelsDir = m_paths.modelsDir();

    QFuture<QPair<Pending, QString>> future =
        QtConcurrent::run([repo, pin, prefer, preferMmproj, preset, token,
                          modelsDir]() -> QPair<Pending, QString> {
            QNetworkAccessManager nam;
            QString err;
            QByteArray auth;
            if (!token.isEmpty())
                auth = QStringLiteral("Bearer %1").arg(token).toUtf8();
            QString rev;
            if (!pin.isEmpty()) {
                rev = pin;
            } else {
                rev = ModelCatalog::fetchHeadSha(&nam, repo, err, auth);
                if (rev.isEmpty())
                    return {Pending{}, err.isEmpty()
                                          ? QObject::tr("Could not resolve repository %1").arg(repo)
                                          : err};
            }
            const QList<HfFile> tree = ModelCatalog::fetchTree(&nam, repo, rev, err, auth);
            if (tree.isEmpty())
                return {Pending{}, err.isEmpty()
                                      ? QObject::tr("No files found in %1").arg(repo)
                                      : err};

            QStringList modelNames;
            QString mmprojRel;
            selectModelFiles(tree, prefer, preferMmproj, &modelNames, mmprojRel);
            if (modelNames.isEmpty())
                return {Pending{}, QObject::tr("No usable model file found in %1").arg(repo)};

            Pending p;
            p.repo = repo;
            p.revision = rev;
            p.title = preset.title.isEmpty() ? repo : preset.title;
            p.license = preset.license;
            p.parser = preset.parser;
            p.prompt = preset.prompt;
            p.ctxSize = preset.ctxSize;
            p.presetId = preset.id;
            p.dir = QDir(modelsDir).filePath(repoDirName(repo));
            p.mmprojRel = mmprojRel;
            p.modelNames = modelNames;
            p.fileSha256 = preset.sha256;  // 4.8: pin per-file digests
            p.files = tree;
            return {std::move(p), QString()};
        });

    future.then(this, [this](const QPair<Pending, QString> &res) {
        setBusy(false);
        onPrepareDone(res.first, res.second);
    });
}

void ModelInstaller::onPrepareDone(const Pending &p, const QString &err)
{
    if (!err.isEmpty()) {
        setStatusMessage(err);
        setState(State::Error);
        return;
    }
    m_pending = p;
    setStatusMessage(tr("Ready: %1 (%2)").arg(p.title, p.repo));
    setState(State::ReadyToDownload);
}

void ModelInstaller::installPrepared()
{
    if (m_state == State::Downloading)
        return;
    if (m_pending.modelNames.isEmpty()) {
        setStatusMessage(tr("Nothing prepared to install"));
        setState(State::Error);
        return;
    }
    beginDownload();
}

void ModelInstaller::beginDownload()
{
    m_paths.ensureDirectories();
    QDir().mkpath(m_pending.dir);
    setState(State::Downloading);
    setBusy(true);
    setProgress(0.0);
    setStatusMessage(tr("Downloading %1 …").arg(m_pending.title));

    m_downloadCount = 0;
    m_downloadDone = 0;
    m_downloadFailed = false;

    QSet<QString> leaves;
    for (const QString &path : m_pending.modelNames)
        leaves.insert(ModelCatalog::leafName(path));
    if (!m_pending.mmprojRel.isEmpty())
        leaves.insert(ModelCatalog::leafName(m_pending.mmprojRel));
    if (leaves.size() < m_pending.modelNames.size()
                          + (m_pending.mmprojRel.isEmpty() ? 0 : 1)) {
        setBusy(false);
        setStatusMessage(tr("Repository contains identically named files in "
                            "different subdirectories; cannot install"));
        setState(State::Error);
        return;
    }

    const QString repo = m_pending.repo;
    const QString rev = m_pending.revision;
    for (const QString &path : m_pending.modelNames)
        enqueueFile(path, repo, rev);
    // Multi-quant installs share the repo folder: skip the projector when the
    // same file is already on disk instead of re-downloading it per quant.
    if (!m_pending.mmprojRel.isEmpty() && !mmprojAlreadyOnDisk())
        enqueueFile(m_pending.mmprojRel, repo, rev);
}

void ModelInstaller::enqueueFile(const QString &repoPath, const QString &repo,
                                 const QString &commitSha)
{
    const QString leaf = ModelCatalog::leafName(repoPath);
    const QUrl url = ModelCatalog::resolveUrl(repo, commitSha, repoPath);
    QString auth;
    const QString token = m_settings.hfToken();
    if (!token.isEmpty())
        auth = QStringLiteral("Bearer %1").arg(token);

    DownloadTask::Request req;
    req.url = url;
    req.targetDir = m_pending.dir;
    req.fileName = leaf;
    req.sha256 = expectedShaFor(repoPath);
    req.authorization = auth;

    const int row = m_downloads->enqueue(req);
    DownloadTask *task = m_downloads->taskAt(row);
    ++m_downloadCount;
    connect(task, &DownloadTask::downloadFinished, this,
            [this](bool ok) { onOneDownloadFinished(ok); });
}

QString ModelInstaller::expectedShaFor(const QString &repoPath) const
{
    const QString leaf = ModelCatalog::leafName(repoPath);
    const QString pinned = m_pending.fileSha256.value(leaf.toLower());
    if (!pinned.isEmpty())
        return pinned;
    for (const HfFile &hf : std::as_const(m_pending.files)) {
        if (hf.path == repoPath)
            return hf.lfsOid;
    }
    return QString();
}

// True when the projector for the pending install is already in the target
// folder and matches what we would download, so the download can be skipped.
bool ModelInstaller::mmprojAlreadyOnDisk() const
{
    const QString target = QDir(m_pending.dir)
                               .filePath(ModelCatalog::leafName(m_pending.mmprojRel));
    const QFileInfo fi(target);
    if (!fi.exists() || fi.size() <= 0)
        return false;

    // The same pinned revision recorded for this folder and this projector
    // means the on-disk file is exactly the one we would fetch.
    if (!m_pending.revision.isEmpty()) {
        for (const ModelEntry &x : std::as_const(m_installed)) {
            if (x.revision == m_pending.revision && x.mmprojPath == target)
                return true;
        }
    }

    // Otherwise trust the pinned digest (lfs.oid / preset sha256) so a stale
    // or corrupt copy is re-downloaded instead of silently reused.
    const QString expected = expectedShaFor(m_pending.mmprojRel);
    if (expected.isEmpty())
        return false;

    QFile f(target);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buf(1 << 20, Qt::Uninitialized);
    qint64 n = 0;
    while ((n = f.read(buf.data(), buf.size())) > 0)
        hash.addData(QByteArrayView(buf.constData(), static_cast<int>(n)));
    f.close();
    if (n < 0)
        return false;
    return QString::fromLatin1(hash.result().toHex()) == expected.toLower();
}

void ModelInstaller::onOneDownloadFinished(bool ok)
{
    ++m_downloadDone;
    if (!ok)
        m_downloadFailed = true;
    emitDownloadProgress();
    if (m_downloadDone == m_downloadCount)
        maybeFinishDownloads();
}

void ModelInstaller::emitDownloadProgress()
{
    const qint64 total = m_downloads->totalBytes();
    const qint64 received = m_downloads->receivedBytes();
    setProgress(total > 0 ? double(received) / double(total) : 0.0);
}

void ModelInstaller::maybeFinishDownloads()
{
    if (m_state != State::Downloading)
        return;
    if (m_downloadFailed) {
        setBusy(false);
        setStatusMessage(tr("Download failed — check your connection and try again"));
        setState(State::Error);
        return;
    }
    completeInstall();
}

void ModelInstaller::completeInstall()
{
    auto localPath = [this](const QString &repoPath) {
        return QDir(m_pending.dir).filePath(ModelCatalog::leafName(repoPath));
    };

    const QString primary = localPath(m_pending.modelNames.first());

    // GGUF magic validation (§ Stage E task 5).
    QFile f(primary);
    if (!f.open(QIODevice::ReadOnly)) {
        setBusy(false);
        setStatusMessage(tr("Unable to read downloaded file %1").arg(primary));
        setState(State::Error);
        return;
    }
    if (!f.read(4).startsWith("GGUF")) {
        setBusy(false);
        setStatusMessage(tr("File %1 is not a valid GGUF (missing magic)").arg(primary));
        setState(State::Error);
        return;
    }
    f.close();

    ModelEntry e;
    {
        const QString baseId = repoDirName(m_pending.repo);
        const QString quant = ModelCatalog::quantizationFromName(
            ModelCatalog::leafName(m_pending.modelNames.first()));
        // Per-quant id: several quants of one repo may coexist in the same
        // directory (shared mmproj) and must not overwrite each other.
        e.id = quant.isEmpty() ? baseId : baseId + QLatin1Char('_') + quant;
    }
    e.title = m_pending.title;
    e.repo = m_pending.repo;
    e.repoId = m_pending.repo;
    e.revision = m_pending.revision;
    e.dir = m_pending.dir;
    e.origin = ModelOrigin::Managed;
    e.modelPath = primary;
    for (const QString &path : m_pending.modelNames) {
        if (path != m_pending.modelNames.first())
            e.parts.append(localPath(path));
    }
    if (!m_pending.mmprojRel.isEmpty())
        e.mmprojPath = localPath(m_pending.mmprojRel);
    e.quantization = ModelCatalog::quantizationFromName(
        ModelCatalog::leafName(m_pending.modelNames.first()));
    e.license = m_pending.license;
    e.parser = m_pending.parser;
    e.prompt = m_pending.prompt;
    e.ctxSize = m_pending.ctxSize;
    e.ctxSizeSet = m_pending.ctxSize > 0;
    e.addedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    qint64 total = 0;
    for (const QString &path : m_pending.modelNames)
        total += QFileInfo(localPath(path)).size();
    if (!m_pending.mmprojRel.isEmpty())
        total += QFileInfo(localPath(m_pending.mmprojRel)).size();
    e.byteSize = total;

    QList<ModelEntry> updated = m_installed;
    updated.removeIf([&](const ModelEntry &x) { return x.id == e.id; });
    updated.append(e);
    QString saveErr;
    if (!ModelRegistry::save(m_paths.modelsDir(), updated, saveErr)) {
        setBusy(false);
        setStatusMessage(tr("Model downloaded, but the registry could not be "
                            "saved: %1").arg(saveErr));
        setState(State::Error);
        return;
    }
    m_installed = updated;

    // Settings are the very last step (§7.2).
    m_settings.setLaunchModelPath(e.modelPath);
    if (!e.mmprojPath.isEmpty())
        m_settings.setLaunchMmprojPath(e.mmprojPath);
    if (!m_pending.presetId.isEmpty())
        m_settings.setLaunchPresetId(m_pending.presetId);
    if (!e.parser.isEmpty())
        m_settings.setParserId(e.parser);
    if (e.ctxSize > 0)
        m_settings.setLaunchCtxSize(e.ctxSize);
    m_settings.forceSave();

    setBusy(false);
    setStatusMessage(tr("Installed %1").arg(e.title));
    emit installedChanged();
    setState(State::Idle);
}

void ModelInstaller::startSearch()
{
    if (m_busy)
        return;
    if (m_searchQuery.trimmed().isEmpty()) {
        setStatusMessage(tr("Enter a search query"));
        setState(State::Error);
        return;
    }
    const QString q = m_searchQuery.trimmed();
    const QString token = m_settings.hfToken();
    m_searchActive = true;
    setBusy(true);
    setState(State::Fetching);
    setStatusMessage(tr("Searching Hugging Face …"));
    emit searchChanged();

    QFuture<QPair<QList<HfModelSummary>, QString>> future =
        QtConcurrent::run([q, token]() -> QPair<QList<HfModelSummary>, QString> {
            QNetworkAccessManager nam;
            QString error;
            QByteArray auth;
            if (!token.isEmpty())
                auth = QStringLiteral("Bearer %1").arg(token).toUtf8();
            const QList<HfModelSummary> res = ModelCatalog::search(&nam, q, error,
                                                                   30, auth);
            return {res, error};
        });

    future.then(this, [this](const QPair<QList<HfModelSummary>, QString> &res) {
        m_searchActive = false;
        setBusy(false);
        if (res.first.isEmpty() && !res.second.isEmpty()) {
            setStatusMessage(res.second);
            setState(State::Error);
            emit searchChanged();
            return;
        }
        m_searchResults = res.first;
        setStatusMessage(res.first.isEmpty()
                             ? tr("No models found")
                             : tr("%1 model(s) found").arg(res.first.size()));
        setState(State::Idle);
        emit searchChanged();
    });
}

QVariantMap ModelInstaller::searchResult(int index) const
{
    QVariantMap out;
    if (index < 0 || index >= m_searchResults.size())
        return out;
    const HfModelSummary &s = m_searchResults.at(index);
    out.insert(QStringLiteral("id"), s.id);
    out.insert(QStringLiteral("title"), s.title.isEmpty() ? s.id : s.title);
    out.insert(QStringLiteral("license"), s.license);
    out.insert(QStringLiteral("downloads"), QVariant::fromValue(s.downloads));
    out.insert(QStringLiteral("gated"), s.gated);
    return out;
}

void ModelInstaller::installRemote(int index)
{
    if (m_busy)
        return;
    if (index < 0 || index >= m_searchResults.size()) {
        setStatusMessage(tr("Invalid search selection"));
        setState(State::Error);
        return;
    }
    const HfModelSummary &s = m_searchResults.at(index);
    ModelPreset p;
    p.id = QStringLiteral("search-%1").arg(s.id);
    p.title = s.title.isEmpty() ? s.id : s.title;
    p.repo = s.id;
    p.license = s.license;
    beginPrepare(p);
}

void ModelInstaller::cancelInstall()
{
    m_downloads->cancelAll(true);
    setBusy(false);
    setStatusMessage(tr("Download canceled"));
    setState(State::Idle);
}

QString ModelInstaller::importCatalog(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return tr("Unable to open catalog: %1").arg(f.errorString());
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError)
        return tr("Catalog is not valid JSON: %1").arg(perr.errorString());

    QJsonArray arr;
    if (doc.isArray())
        arr = doc.array();
    else if (doc.isObject())
        arr = doc.object().value(QStringLiteral("models")).toArray();
    else
        return tr("Unexpected catalog shape");

    QString err;
    const QList<ModelPreset> incoming = ModelPresetCatalog::parse(arr, err);
    if (incoming.isEmpty())
        return err.isEmpty() ? tr("No valid presets in file") : err;

    QString loadErr;
    const QString userPath =
        QDir(m_paths.modelsDir()).filePath(QStringLiteral("catalog.json"));
    QList<ModelPreset> userCatalog = ModelPresetCatalog::load(userPath, loadErr);
    for (const ModelPreset &p : incoming) {
        userCatalog.removeIf([&](const ModelPreset &x) { return x.id == p.id; });
        userCatalog.append(p);
    }
    // Drop entries that merely restate a built-in preset (same id and content).
    const QList<ModelPreset> builtIn = ModelPresetCatalog::load(
        ModelPresetCatalog::kBuiltInPath, loadErr);
    userCatalog.removeIf([&](const ModelPreset &u) {
        return std::any_of(builtIn.cbegin(), builtIn.cend(),
                           [&](const ModelPreset &b) {
                               return b.id == u.id && b.toJson() == u.toJson();
                           });
    });
    if (!ModelPresetCatalog::save(userPath, userCatalog, err))
        return err;
    reloadPresetsInternal();
    return QString();
}

QString ModelInstaller::exportCatalog(const QString &path)
{
    QString err;
    if (!ModelPresetCatalog::save(path, m_presets, err))
        return err;
    return QString();
}

QString ModelInstaller::resetUserCatalog()
{
    QString err;
    const QString userPath =
        QDir(m_paths.modelsDir()).filePath(QStringLiteral("catalog.json"));
    if (!ModelPresetCatalog::resetUserCatalog(userPath, err))
        return err;
    reloadPresetsInternal();
    return QString();
}

QString ModelInstaller::hfToken() const
{
    return m_settings.hfToken();
}

void ModelInstaller::setHfToken(const QString &token)
{
    m_settings.setHfToken(token);
}

}  // namespace llocr