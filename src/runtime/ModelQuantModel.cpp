#include "runtime/ModelQuantModel.h"

#include <QFileInfo>
#include <QSet>
#include <QStringList>

#include <algorithm>

#include "config/SettingsStore.h"
#include "core/ByteSize.h"
#include "core/LaunchProfile.h"
#include "core/ModelProfiles.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/ModelCatalog.h"
#include "runtime/ModelInstaller.h"
#include "runtime/ModelMemoryEstimator.h"
#include "runtime/RuntimeController.h"

namespace llocr {

namespace {

QString modelStem(const QString &pathOrFile)
{
    const QString leaf = ModelCatalog::leafName(pathOrFile);
    QString base;
    int index = 0;
    int count = 0;
    if (ModelCatalog::splitMultiPart(leaf, &base, &index, &count))
        return base;
    return leaf;
}

QString quantLabelOf(const ModelEntry &entry)
{
    QString quant = entry.quantization.trimmed();
    if (quant.isEmpty())
        quant = ModelCatalog::quantizationForFile(entry.repo, ModelCatalog::leafName(entry.modelPath)).trimmed();
    if (!quant.isEmpty())
        return quant.toUpper();
    const QString stem = modelStem(entry.modelPath);
    return stem.isEmpty() ? entry.title : stem;
}

int indexOfStem(const QList<ModelEntry> &installed, const QString &repo, const QString &stem)
{
    for (int i = 0; i < installed.size(); ++i) {
        const ModelEntry &entry = installed.at(i);
        if (entry.modelPath.isEmpty() || entry.repo != repo)
            continue;
        if (modelStem(entry.modelPath) == stem)
            return i;
    }
    return -1;
}

int indexOfPreset(const QList<ModelPreset> &presets, const QString &profileId, const QString &stem)
{
    for (int i = 0; i < presets.size(); ++i) {
        const ModelPreset &preset = presets.at(i);
        if (!preset.profileId.isEmpty() && preset.profileId != profileId)
            continue;
        if (modelStem(preset.model) == stem)
            return i;
    }
    return -1;
}

ModelQuantModel::Quant quantOf(const QList<ModelEntry> &installed,
                               const QList<ModelPreset> &presets,
                               const QString &profileId,
                               const QString &repo,
                               const QString &stem,
                               const QString &label,
                               qint64 presetSize,
                               const QString &activePath)
{
    ModelQuantModel::Quant quant;
    quant.id = label;
    quant.entryIndex = indexOfStem(installed, repo, stem);
    if (quant.entryIndex >= 0) {
        const ModelEntry &entry = installed.at(quant.entryIndex);
        quant.size = entry.byteSize;
        quant.active = entry.modelPath == activePath;
    } else {
        quant.size = presetSize;
    }
    quant.presetIndex = indexOfPreset(presets, profileId, stem);
    return quant;
}

// The management list is role-agnostic: a model is measured in the role that
// asks the most of it — the layout task if it has one (a page fills the image
// budget), its first role otherwise.
QString effectiveRole(const ModelProfiles::Profile &profile, const QString &role)
{
    if (role != QLatin1String("all"))
        return role;
    QStringList names = profile.roles.keys();
    std::sort(names.begin(), names.end());
    for (const QString &name : std::as_const(names)) {
        if (name == QLatin1String("layout"))
            return name;
    }
    return names.isEmpty() ? role : names.first();
}

QVariantMap quantMap(const ModelQuantModel::Quant &q)
{
    QVariantMap item;
    item.insert(QStringLiteral("id"), q.id);
    item.insert(QStringLiteral("size"), QVariant::fromValue(q.size));
    item.insert(QStringLiteral("sizeLabel"), ::llocr::formatBytes(q.size));
    item.insert(QStringLiteral("vramBytes"), QVariant::fromValue(q.vramBytes));
    item.insert(QStringLiteral("vramLabel"), ::llocr::formatBytes(q.vramBytes));
    item.insert(QStringLiteral("installed"), q.entryIndex >= 0);
    item.insert(QStringLiteral("active"), q.active);
    item.insert(QStringLiteral("downloadable"), q.presetIndex >= 0);
    return item;
}

}  // namespace

ModelQuantModel::ModelQuantModel(ModelInstaller &installer, SettingsStore &settings, QString role, QObject *parent)
    : QAbstractListModel(parent), m_installer(installer), m_settings(settings), m_role(std::move(role))
{
}

int ModelQuantModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> ModelQuantModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {ProfileRole, "profile"},
        {LicenseRole, "license"},
        {RuntimeNoteRole, "runtimeNote"},
        {QuantsRole, "quants"},
        {SelectedRole, "selected"},
        {SelectedLabelRole, "selectedLabel"},
        {SelectedInstalledRole, "selectedInstalled"},
        {SelectedActiveRole, "selectedActive"},
        {SelectedSizeRole, "selectedSize"},
        {SelectedDownloadableRole, "selectedDownloadable"},
        {ActiveRole, "active"},
    };
}

QVariant ModelQuantModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    const Quant *quant = row.selected >= 0 && row.selected < row.quants.size() ? &row.quants.at(row.selected) : nullptr;
    switch (role) {
    case KeyRole:
        return row.key;
    case TitleRole:
        return row.title;
    case SubtitleRole:
        return row.subtitle;
    case ProfileRole:
        return row.profile;
    case LicenseRole:
        return row.license;
    case RuntimeNoteRole:
        return row.runtimeNote;
    case QuantsRole: {
        QVariantList out;
        out.reserve(row.quants.size());
        for (const Quant &q : row.quants)
            out.append(quantMap(q));
        return out;
    }
    case SelectedRole:
        return row.selected;
    case SelectedLabelRole:
        return quant ? quant->id : QString();
    case SelectedInstalledRole:
        return quant && quant->entryIndex >= 0;
    case SelectedActiveRole:
        return quant && quant->active;
    case SelectedSizeRole:
        return quant ? QVariant::fromValue(quant->size) : QVariant();
    case SelectedDownloadableRole:
        return quant && quant->presetIndex >= 0;
    case ActiveRole: {
        for (const Quant &q : row.quants) {
            if (q.active)
                return true;
        }
        return false;
    }
    }
    return {};
}

void ModelQuantModel::refresh()
{
    const QList<Row> rows = buildRows();
    const QString signature = signatureOf(rows);
    const int previousCount = m_rows.size();

    if (signature == m_signature && rows.size() == previousCount) {
        m_rows = rows;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0), allRoles());
        return;
    }

    beginResetModel();
    m_rows = rows;
    m_signature = signature;
    endResetModel();
    if (rows.size() != previousCount)
        emit countChanged();
}

qint64 ModelQuantModel::requiredVram(const ModelProfiles::Profile &profile, qint64 weightsBytes, qint64 projectorBytes) const
{
    if (!profile.memory.valid)
        return 0;
    const QString role = effectiveRole(profile, m_role);
    const LaunchProfile launch = m_installer.runtime().launchProfiles().activeProfile(profile.id, role);
    const int visionTokens = ModelProfiles::visionTokensFor(profile, role);
    ModelMemoryShape shape = shapeFromProfile(profile.memory);
    shape.weightsBytes = weightsBytes;
    shape.projectorBytes = projectorBytes;
    shape.draftBytes = profile.memory.draftBytes;
    const ModelMemoryBudget budget = budgetFromLaunch(launch, visionTokens);
    return estimateMemory(shape, budget).requiredVramBytes;
}

QList<ModelQuantModel::Row> ModelQuantModel::buildRows() const
{
    // The management list ("all", the rework plan's stage 7) shows every
    // profile and every installed entry regardless of the task heuristics,
    // and marks nothing active — assigning models to tasks lives in the task
    // window.
    const bool all = m_role == QLatin1String("all");
    const QString activePath = all ? QString() : m_settings.launchModelPathForRole(m_role);
    const QList<ModelPreset> &presets = m_installer.presetsForRole(m_role);
    const QList<ModelEntry> &installed = m_installer.installedEntries();

    const QList<ModelProfiles::Profile> profiles = all ? ModelProfiles::instance() : ModelProfiles::forRole(ModelProfiles::instance(), m_role);
    QList<Row> rows;
    for (const ModelProfiles::Profile &profile : std::as_const(profiles)) {
        Row row;
        row.key = profile.id;
        row.title = profile.title;
        row.subtitle = profile.files.repo;
        row.license = profile.license;
        row.runtimeNote = profile.runtimeNote;
        row.profile = true;

        // The projector the model needs: the file on disk when it is
        // installed, the curated size otherwise.
        qint64 projectorBytes = profile.files.mmproj.sizeBytes;
        for (const ModelEntry &entry : installed) {
            if (entry.repo == profile.files.repo && !entry.mmprojPath.isEmpty()) {
                projectorBytes = QFileInfo(entry.mmprojPath).size();
                break;
            }
        }

        QSet<QString> covered;
        for (const ModelProfiles::Quant &quant : profile.files.quants) {
            const QString stem = modelStem(quant.file);
            row.quants.append(quantOf(installed, presets, profile.id, profile.files.repo, stem, quant.id.toUpper(), quant.sizeBytes, activePath));
            covered.insert(stem);
        }
        for (int i = 0; i < installed.size(); ++i) {
            const ModelEntry &entry = installed.at(i);
            const QString stem = modelStem(entry.modelPath);
            if (entry.repo != profile.files.repo || covered.contains(stem))
                continue;
            if (!all && !matchesRole(entry, m_settings, m_role))
                continue;
            row.quants.append(quantOf(installed, presets, profile.id, profile.files.repo, stem, quantLabelOf(entry), 0, activePath));
            covered.insert(stem);
        }
        for (Quant &quant : row.quants)
            quant.vramBytes = requiredVram(profile, quant.size, projectorBytes);
        if (!row.quants.isEmpty())
            rows.append(row);
    }

    for (int i = 0; i < installed.size(); ++i) {
        const ModelEntry &entry = installed.at(i);
        if (!entry.repo.isEmpty() && !ModelProfiles::idForRepo(ModelProfiles::instance(), entry.repo).isEmpty())
            continue;
        if (!all && !matchesRole(entry, m_settings, m_role))
            continue;
        Row row;
        row.key = entry.id;
        row.title = displayTitle(entry);
        row.subtitle = entry.dir.isEmpty() ? QFileInfo(entry.modelPath).absolutePath() : entry.dir;
        row.license = entry.license;
        Quant quant;
        quant.id = quantLabelOf(entry);
        quant.entryIndex = i;
        quant.size = entry.byteSize;
        quant.active = !entry.modelPath.isEmpty() && entry.modelPath == activePath;
        // A model outside the catalog has no profile: nothing is known about
        // the encoder it would run, so no card is named.
        row.quants.append(quant);
        rows.append(row);
    }

    for (Row &row : rows)
        row.selected = selectedIndex(row);
    return rows;
}

int ModelQuantModel::selectedIndex(const Row &row) const
{
    if (row.quants.isEmpty())
        return -1;
    // The task's own active quantization leads. The saved pick is one per
    // profile, so two tasks riding one profile must not show each other's
    // choice; the remembered value is the default only while the task is not
    // riding one of the row's quants yet.
    for (int i = 0; i < row.quants.size(); ++i) {
        if (row.quants.at(i).active)
            return i;
    }
    if (row.profile) {
        const QString saved = m_settings.selectedQuant(row.key);
        for (int i = 0; !saved.isEmpty() && i < row.quants.size(); ++i) {
            if (row.quants.at(i).id.compare(saved, Qt::CaseInsensitive) == 0)
                return i;
        }
    }
    for (int i = 0; i < row.quants.size(); ++i) {
        if (row.quants.at(i).entryIndex >= 0)
            return i;
    }
    return 0;
}

QString ModelQuantModel::signatureOf(const QList<Row> &rows)
{
    QStringList parts;
    parts.reserve(rows.size());
    for (const Row &row : rows) {
        QStringList quants;
        quants.reserve(row.quants.size());
        for (const Quant &quant : row.quants)
            quants.append(quant.id);
        parts.append(row.key + QLatin1Char('|') + quants.join(QLatin1Char(',')));
    }
    return parts.join(QLatin1Char(';'));
}

QList<int> ModelQuantModel::allRoles() const
{
    QList<int> ids;
    const QHash<int, QByteArray> names = roleNames();
    ids.reserve(names.size());
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        ids.append(it.key());
    return ids;
}

const ModelQuantModel::Row *ModelQuantModel::findRow(const QString &key) const
{
    for (const Row &row : std::as_const(m_rows)) {
        if (row.key == key)
            return &row;
    }
    return nullptr;
}

const ModelQuantModel::Quant *ModelQuantModel::quantFor(const QString &key, const QString &quantId) const
{
    const Row *row = findRow(key);
    if (!row)
        return nullptr;
    for (const Quant &quant : row->quants) {
        if (quant.id.compare(quantId, Qt::CaseInsensitive) == 0)
            return &quant;
    }
    return nullptr;
}

int ModelQuantModel::entryIndexFor(const QString &key, const QString &quantId) const
{
    const Quant *quant = quantFor(key, quantId);
    return quant ? quant->entryIndex : -1;
}

int ModelQuantModel::rowIndexOf(const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).key == key)
            return i;
    }
    return -1;
}

QVariantMap ModelQuantModel::rowFor(const QString &key) const
{
    const Row *row = findRow(key);
    if (!row)
        return {};
    QVariantMap out;
    out.insert(QStringLiteral("key"), row->key);
    out.insert(QStringLiteral("title"), row->title);
    out.insert(QStringLiteral("subtitle"), row->subtitle);
    out.insert(QStringLiteral("license"), row->license);
    out.insert(QStringLiteral("runtimeNote"), row->runtimeNote);
    out.insert(QStringLiteral("active"), std::any_of(row->quants.cbegin(), row->quants.cend(), [](const Quant &q) { return q.active; }));
    QVariantList quants;
    quants.reserve(row->quants.size());
    for (const Quant &q : row->quants)
        quants.append(quantMap(q));
    out.insert(QStringLiteral("quants"), quants);
    out.insert(QStringLiteral("selected"), row->selected);
    return out;
}

QList<int> ModelQuantModel::entryIndexesFor(const QString &key) const
{
    QList<int> indexes;
    const Row *row = findRow(key);
    if (!row)
        return indexes;
    for (const Quant &quant : row->quants) {
        if (quant.entryIndex >= 0)
            indexes.append(quant.entryIndex);
    }
    return indexes;
}

int ModelQuantModel::presetIndexFor(const QString &key, const QString &quantId) const
{
    const Quant *quant = quantFor(key, quantId);
    return quant ? quant->presetIndex : -1;
}

bool ModelQuantModel::matchesRole(const ModelEntry &entry, const SettingsStore &settings, const QString &role)
{
    const bool active = !entry.modelPath.isEmpty() && entry.modelPath == settings.launchModelPathForRole(role);
    if (active)
        return true;
    if (!entry.roles.isEmpty())
        return entry.roles.contains(role);
    // Legacy entries carry no task tags; they predate the task rework, so the
    // recognition task gets the vision-capability heuristic: a text-only file
    // read as a block model, a projector-bearing one as the page task's — and
    // the page task is gone, so the projector-bearing entry stays out of the
    // role lists until the user re-activates it from Settings → Models.
    if (role != QLatin1String("blockRecognition"))
        return false;
    return entry.mmprojPath.isEmpty();
}

QString ModelQuantModel::stemName(const QString &pathOrFile)
{
    return modelStem(pathOrFile);
}

QString ModelQuantModel::displayTitle(const ModelEntry &entry)
{
    QString display = QFileInfo(entry.modelPath).completeBaseName();
    const QString quant = entry.quantization.trimmed();
    if (!quant.isEmpty() && display.endsWith(QLatin1Char('-') + quant))
        display.chop(quant.size() + 1);
    if (display.isEmpty())
        display = entry.title.isEmpty() ? entry.repo : entry.title;
    if (!quant.isEmpty())
        display += QLatin1Char(' ') + quant;
    return display;
}

}  // namespace llocr
