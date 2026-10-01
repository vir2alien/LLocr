#include "runtime/ModelQuantModel.h"

#include <QFileInfo>
#include <QSet>
#include <QStringList>

#include <algorithm>

#include "config/SettingsStore.h"
#include "core/ModelProfiles.h"
#include "runtime/ModelCatalog.h"
#include "runtime/ModelInstaller.h"

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
        quant = ModelCatalog::quantizationFromName(entry.modelPath).trimmed();
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

ModelQuantModel::Quant
quantOf(const QList<ModelEntry> &installed, const QList<ModelPreset> &presets, const QString &profileId, const QString &repo, const QString &stem, const QString &label, const QString &activePath)
{
    ModelQuantModel::Quant quant;
    quant.id = label;
    quant.entryIndex = indexOfStem(installed, repo, stem);
    if (quant.entryIndex >= 0) {
        const ModelEntry &entry = installed.at(quant.entryIndex);
        quant.size = entry.byteSize;
        quant.active = entry.modelPath == activePath;
    }
    quant.presetIndex = indexOfPreset(presets, profileId, stem);
    return quant;
}

}  // namespace

ModelQuantModel::ModelQuantModel(ModelInstaller &installer, SettingsStore &settings, bool forCheck, QObject *parent)
    : QAbstractListModel(parent), m_installer(installer), m_settings(settings), m_forCheck(forCheck)
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
        for (const Quant &q : row.quants) {
            QVariantMap item;
            item.insert(QStringLiteral("id"), q.id);
            item.insert(QStringLiteral("size"), QVariant::fromValue(q.size));
            item.insert(QStringLiteral("installed"), q.entryIndex >= 0);
            item.insert(QStringLiteral("active"), q.active);
            item.insert(QStringLiteral("downloadable"), q.presetIndex >= 0);
            out.append(item);
        }
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
    default:
        return {};
    }
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

QList<ModelQuantModel::Row> ModelQuantModel::buildRows() const
{
    const QString role = m_forCheck ? QStringLiteral("check") : QStringLiteral("ocr");
    const QString activePath = m_forCheck ? m_settings.checkLaunchModelPath() : m_settings.launchModelPath();
    const QList<ModelPreset> &presets = m_installer.presetsForRole(m_forCheck);
    const QList<ModelEntry> &installed = m_installer.installedEntries();

    const QList<ModelProfiles::Profile> profiles = ModelProfiles::forRole(ModelProfiles::instance(), role);
    QList<Row> rows;
    for (const ModelProfiles::Profile &profile : std::as_const(profiles)) {
        Row row;
        row.key = profile.id;
        row.title = profile.title;
        row.subtitle = profile.files.repo;
        row.license = profile.license;
        row.runtimeNote = profile.runtimeNote;
        row.profile = true;

        QSet<QString> covered;
        for (const ModelProfiles::Quant &quant : profile.files.quants) {
            const QString stem = modelStem(quant.file);
            row.quants.append(quantOf(installed, presets, profile.id, profile.files.repo, stem, quant.id.toUpper(), activePath));
            covered.insert(stem);
        }
        for (int i = 0; i < installed.size(); ++i) {
            const ModelEntry &entry = installed.at(i);
            const QString stem = modelStem(entry.modelPath);
            if (entry.repo != profile.files.repo || covered.contains(stem))
                continue;
            if (!matchesRole(entry, m_settings, m_forCheck))
                continue;
            row.quants.append(quantOf(installed, presets, profile.id, profile.files.repo, stem, quantLabelOf(entry), activePath));
            covered.insert(stem);
        }
        if (!row.quants.isEmpty())
            rows.append(row);
    }

    for (int i = 0; i < installed.size(); ++i) {
        const ModelEntry &entry = installed.at(i);
        if (!entry.repo.isEmpty() && !ModelProfiles::idForRepo(ModelProfiles::instance(), entry.repo).isEmpty())
            continue;
        if (!matchesRole(entry, m_settings, m_forCheck))
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
    if (row.profile) {
        const QString saved = m_settings.selectedQuant(row.key);
        for (int i = 0; !saved.isEmpty() && i < row.quants.size(); ++i) {
            if (row.quants.at(i).id.compare(saved, Qt::CaseInsensitive) == 0)
                return i;
        }
    }
    for (int i = 0; i < row.quants.size(); ++i) {
        if (row.quants.at(i).active)
            return i;
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

const ModelQuantModel::Row *ModelQuantModel::rowFor(const QString &key) const
{
    for (const Row &row : std::as_const(m_rows)) {
        if (row.key == key)
            return &row;
    }
    return nullptr;
}

const ModelQuantModel::Quant *ModelQuantModel::quantFor(const QString &key, const QString &quantId) const
{
    const Row *row = rowFor(key);
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

QList<int> ModelQuantModel::entryIndexesFor(const QString &key) const
{
    QList<int> indexes;
    const Row *row = rowFor(key);
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

bool ModelQuantModel::matchesRole(const ModelEntry &entry, const SettingsStore &settings, bool forCheck)
{
    const bool ocrActive = !entry.modelPath.isEmpty() && entry.modelPath == settings.launchModelPath();
    const bool checkActive = !entry.modelPath.isEmpty() && entry.modelPath == settings.checkLaunchModelPath();

    if (forCheck ? checkActive : ocrActive)
        return true;
    if (!entry.roles.isEmpty())
        return entry.roles.contains(forCheck ? QStringLiteral("check") : QStringLiteral("ocr"));
    if (checkActive)
        return false;
    const bool vision = !entry.mmprojPath.isEmpty();
    return forCheck ? !vision : vision;
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
