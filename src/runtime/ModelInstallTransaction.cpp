#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QRegularExpression>

#include <QSet>
#include <algorithm>
#include <QtConcurrent>
#include <utility>

#include "app/LaunchProfileStore.h"
#include "app/SettingsStore.h"
#include "runtime/DownloadGroup.h"
#include "runtime/DownloadManager.h"
#include "runtime/ModelInstallTransaction.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

QString ModelInstallTransaction::repoDirName(const QString &repo)
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

void ModelInstallTransaction::selectModelFiles(const QList<HfFile> &tree,
                                               const QString &prefer,
                                               const QString &preferMmproj,
                                               QStringList *modelPaths,
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
        if (it.value().second > groupBest
            || (it.value().second == groupBest && it.key() < largestGroup)) {
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

ModelInstallTransaction::ModelInstallTransaction(SettingsStore &settings,
                                                 LaunchProfileStore &launchProfiles,
                                                 QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_launchProfiles(launchProfiles)
    , m_downloads(new DownloadManager(this))
{
    m_group = new DownloadGroup(m_downloads, this);
    connect(m_group, &DownloadGroup::progressChanged, this,
            [this]() { setProgress(m_group->progress()); });
    connect(m_group, &DownloadGroup::allFinished, this, [this](bool) {
        maybeFinishDownloads();
    });
}

ModelInstallTransaction::~ModelInstallTransaction()
{
    if (m_downloads)
        m_downloads->cancelAll(true);
}

void ModelInstallTransaction::shutdown()
{
    if (m_downloads)
        m_downloads->cancelAll(true);
}

void ModelInstallTransaction::setState(State next)
{
    if (m_state == next)
        return;
    m_state = next;
    emit stateChanged(static_cast<int>(next));
}

void ModelInstallTransaction::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

void ModelInstallTransaction::setProgress(double p)
{
    if (qFuzzyCompare(m_progress, p) || p < 0.0 || p > 1.0)
        return;
    m_progress = p;
    emit progressChanged(p);
}

void ModelInstallTransaction::setStatusMessage(const QString &msg)
{
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged(msg);
}

void ModelInstallTransaction::retranslate()
{
    switch (m_state) {
    case State::Downloading:
        setStatusMessage(tr("Downloading %1 …").arg(m_pending.title));
        break;
    case State::ReadyToDownload:
        setStatusMessage(tr("Ready: %1 (%2)").arg(m_pending.title, m_pending.repo));
        break;
    case State::Idle:
    case State::Fetching:
    case State::Error:
        break;
    }
}

void ModelInstallTransaction::prepare(const ModelPreset &preset, bool forCheck)
{
    if (m_busy)
        return;
    m_pendingForCheck = forCheck;
    beginPrepare(preset);
}

void ModelInstallTransaction::beginPrepare(const ModelPreset &preset)
{
    setBusy(true);
    setState(State::Fetching);
    setStatusMessage(tr("Looking up %1 …").arg(preset.repo));

    const QString repo = preset.repo;
    const QString pin = preset.revision;
    const QString prefer = preset.model;
    const QString preferMmproj = preset.mmproj;

    const QString token = m_settings.hfToken();
    const RuntimePaths currentPaths(m_settings.runtimeRootDir(),
                                    m_settings.runtimeModelsDir());
    const QString modelsDir = currentPaths.modelsDir();

    QFuture<QPair<InstallPlan, QString>> future =
        QtConcurrent::run([repo, pin, prefer, preferMmproj, preset, token,
                          modelsDir]() -> QPair<InstallPlan, QString> {
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
                    return {InstallPlan{}, err.isEmpty()
                                          ? QObject::tr("Could not resolve repository %1").arg(repo)
                                          : err};
            }
            const QList<HfFile> tree = ModelCatalog::fetchTree(&nam, repo, rev, err, auth);
            if (tree.isEmpty())
                return {InstallPlan{}, err.isEmpty()
                                      ? QObject::tr("No files found in %1").arg(repo)
                                      : err};

            QStringList modelNames;
            QString mmprojRel;
            selectModelFiles(tree, prefer, preferMmproj, &modelNames, mmprojRel);
            if (modelNames.isEmpty())
                return {InstallPlan{}, QObject::tr("No usable model file found in %1").arg(repo)};

            InstallPlan p;
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
            p.fileSha256 = preset.sha256;
            p.files = tree;
            return {std::move(p), QString()};
        });

    const int generation = ++m_prepareGeneration;

    future.then(this, [this, generation](const QPair<InstallPlan, QString> &res) {
        if (generation != m_prepareGeneration)
            return;
        setBusy(false);
        onPrepareDone(res.first, res.second);
    });
}

void ModelInstallTransaction::onPrepareDone(const InstallPlan &p, const QString &err)
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

void ModelInstallTransaction::installPrepared()
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

void ModelInstallTransaction::beginDownload()
{
    const RuntimePaths currentPaths(m_settings.runtimeRootDir(),
                                    m_settings.runtimeModelsDir());
    currentPaths.ensureDirectories();
    QDir().mkpath(m_pending.dir);
    setState(State::Downloading);
    setBusy(true);
    setProgress(0.0);
    setStatusMessage(tr("Downloading %1 …").arg(m_pending.title));

    m_group->begin();

    QSet<QString> leaves;
    for (const QString &path : std::as_const(m_pending.modelNames))
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
    for (const QString &path : std::as_const(m_pending.modelNames)) {
        enqueueFile(path, repo, rev);
        if (m_state != State::Downloading)
            return;
    }
    if (m_state == State::Downloading && !m_pending.mmprojRel.isEmpty()
        && !mmprojAlreadyOnDisk())
        enqueueFile(m_pending.mmprojRel, repo, rev);
}

void ModelInstallTransaction::enqueueFile(const QString &repoPath, const QString &repo,
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

    m_group->enqueue(req);
}

QString ModelInstallTransaction::expectedShaFor(const QString &repoPath) const
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

bool ModelInstallTransaction::mmprojAlreadyOnDisk() const
{
    const QString target = QDir(m_pending.dir)
                               .filePath(ModelCatalog::leafName(m_pending.mmprojRel));
    const QFileInfo fi(target);
    if (!fi.exists() || fi.size() <= 0)
        return false;

    if (!m_pending.revision.isEmpty()) {
        for (const ModelEntry &x : std::as_const(m_installed)) {
            if (x.revision == m_pending.revision && x.mmprojPath == target)
                return true;
        }
    }

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

void ModelInstallTransaction::maybeFinishDownloads()
{
    if (m_state != State::Downloading)
        return;
    if (m_group->failed()) {
        setBusy(false);
        setStatusMessage(tr("Download failed — check your connection and try again"));
        setState(State::Error);
        return;
    }
    completeInstall();
}

void ModelInstallTransaction::completeInstall()
{
    auto localPath = [this](const QString &repoPath) {
        return QDir(m_pending.dir).filePath(ModelCatalog::leafName(repoPath));
    };

    const QString primary = localPath(m_pending.modelNames.first());

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
        e.id = quant.isEmpty() ? baseId : baseId + QLatin1Char('_') + quant;
    }
    e.title = m_pending.title;
    e.repo = m_pending.repo;
    e.repoId = m_pending.repo;
    e.revision = m_pending.revision;
    e.dir = m_pending.dir;
    e.origin = ModelOrigin::Managed;
    e.modelPath = primary;
    for (const QString &path : std::as_const(m_pending.modelNames)) {
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

    for (const ModelEntry &x : std::as_const(m_installed)) {
        if (x.id != e.id)
            continue;
        for (const QString &r : x.roles)
            if (!e.roles.contains(r))
                e.roles.append(r);
    }
    const QString installRole =
        m_pendingForCheck ? QStringLiteral("check") : QStringLiteral("ocr");
    if (!e.roles.contains(installRole))
        e.roles.append(installRole);

    qint64 total = 0;
    for (const QString &path : std::as_const(m_pending.modelNames))
        total += QFileInfo(localPath(path)).size();
    if (!m_pending.mmprojRel.isEmpty())
        total += QFileInfo(localPath(m_pending.mmprojRel)).size();
    e.byteSize = total;

    QList<ModelEntry> updated = m_installed;
    updated.removeIf([&](const ModelEntry &x) { return x.id == e.id; });
    updated.append(e);
    QString saveErr;
    const QString pendingModelsDir = QFileInfo(m_pending.dir).absolutePath();
    if (!ModelRegistry::save(pendingModelsDir, updated, saveErr)) {
        setBusy(false);
        setStatusMessage(tr("Model downloaded, but the registry could not be "
                            "saved: %1").arg(saveErr));
        setState(State::Error);
        return;
    }
    m_installed = updated;

    if (m_pendingForCheck) {
        m_settings.setCheckLaunchModelPath(e.modelPath);
        if (!e.mmprojPath.isEmpty())
            m_settings.setCheckLaunchMmprojPath(e.mmprojPath);
    } else {
        m_settings.setLaunchModelPath(e.modelPath);
        if (!e.mmprojPath.isEmpty())
            m_settings.setLaunchMmprojPath(e.mmprojPath);
        if (!m_pending.presetId.isEmpty())
            m_settings.setLaunchPresetId(m_pending.presetId);
        if (!e.parser.isEmpty())
            m_settings.setParserId(e.parser);
        if (e.ctxSize > 0)
            m_launchProfiles.setActiveProfileNumber(QStringLiteral("ctx-size"),
                                                    e.ctxSize);
    }
    m_settings.forceSave();

    setBusy(false);
    setStatusMessage(tr("Installed %1").arg(e.title));
    emit installedListReplaced(m_installed);
    setState(State::Idle);
    emit installFinished();
}

void ModelInstallTransaction::cancel()
{
    ++m_prepareGeneration;
    m_downloads->cancelAll(true);
    setBusy(false);
    setStatusMessage(tr("Download canceled"));
    setState(State::Idle);
}

}  // namespace llocr
