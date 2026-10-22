#include "runtime/InstalledFilesModel.h"

#include <QFileInfo>

#include "core/ByteSize.h"
#include "core/ModelProfiles.h"
#include "runtime/ModelCatalog.h"
#include "runtime/ModelInstaller.h"
#include "runtime/ModelQuantModel.h"

namespace llocr {

namespace {

QString entryTitle(const ModelEntry &entry)
{
    // The profile's display title for the repo (what the rows above show);
    // an unknown repo degrades to its HF form (org__repo → org/repo), a
    // hand-brought model to its file stem.
    const QString profileId = ModelProfiles::idForRepo(ModelProfiles::instance(), entry.repo);
    if (const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), profileId))
        return profile->title;
    if (!entry.repo.isEmpty())
        return QString(entry.repo).replace(QLatin1String("__"), QLatin1String("/"));
    return ModelQuantModel::displayTitle(entry);
}

QString entryQuant(const ModelEntry &entry)
{
    QString quant = entry.quantization.trimmed();
    if (quant.isEmpty())
        quant = ModelCatalog::quantizationForFile(entry.repo, ModelCatalog::leafName(entry.modelPath)).trimmed();
    return quant.isEmpty() ? QStringLiteral("—") : quant.toUpper();
}

}  // namespace

InstalledFilesModel::InstalledFilesModel(ModelInstaller &installer, QObject *parent) : QAbstractListModel(parent), m_installer(installer) {}

int InstalledFilesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QHash<int, QByteArray> InstalledFilesModel::roleNames() const
{
    return {
        {EntryIdRole, "entryId"},
        {TitleRole, "title"},
        {QuantRole, "quant"},
        {SizeBytesRole, "sizeBytes"},
        {SizeLabelRole, "sizeLabel"},
        {UsageCountRole, "usageCount"},
        {UsageTextRole, "usageText"},
        {RemovableRole, "removable"},
        {NoteRole, "note"},
    };
}

QVariant InstalledFilesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const ModelEntry &entry = m_entries.at(index.row());
    switch (role) {
    case EntryIdRole:
        return entry.id;
    case TitleRole:
        return entryTitle(entry);
    case QuantRole:
        return entryQuant(entry);
    case SizeBytesRole:
        return QVariant::fromValue(entry.byteSize);
    case SizeLabelRole:
        return formatBytes(entry.byteSize);
    case UsageCountRole:
        return m_installer.activeTaskCount(entry);
    case UsageTextRole: {
        const int count = m_installer.activeTaskCount(entry);
        return count > 0 ? tr("Used by %n task(s)", "", count) : tr("Not used");
    }
    case RemovableRole:
        return m_installer.removalBlockerFor(entry).isEmpty();
    case NoteRole:
        return m_installer.removalBlockerFor(entry);
    }
    return {};
}

void InstalledFilesModel::setSource(QList<ModelEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

void InstalledFilesModel::refresh()
{
    if (m_entries.isEmpty())
        return;
    emit dataChanged(index(0, 0), index(m_entries.size() - 1, 0));
}

}  // namespace llocr
