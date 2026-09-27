#include "runtime/InstalledModelsModel.h"

#include <QFileInfo>

#include "app/SettingsStore.h"

namespace llocr {

InstalledModelsModel::InstalledModelsModel(SettingsStore &settings, bool forCheck,
                                           QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
    , m_forCheck(forCheck)
{
}

int InstalledModelsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> InstalledModelsModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { PathRole, "path" },
        { MmprojPathRole, "mmprojPath" },
        { SizeRole, "size" },
        { QuantizationRole, "quantization" },
        { OriginRole, "origin" },
        { LicenseRole, "license" },
        { RepoRole, "repo" },
        { ActiveRole, "active" },
        { PartsRole, "parts" },
    };
}

QVariant InstalledModelsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    // m_rows is rebuilt by setEntries; an out-of-range hit is a caller bug.
    const ModelEntry &entry = m_entries.at(m_rows.at(index.row()));
    switch (role) {
    case TitleRole:
        return displayTitle(entry);
    case PathRole:
        return entry.modelPath;
    case MmprojPathRole:
        return entry.mmprojPath;
    case SizeRole:
        return QVariant::fromValue(entry.byteSize);
    case QuantizationRole:
        return entry.quantization;
    case OriginRole:
        return entry.origin == ModelOrigin::Managed ? QStringLiteral("managed")
                                                    : QStringLiteral("external");
    case LicenseRole:
        return entry.license;
    case RepoRole:
        return entry.repo;
    case ActiveRole:
        return isActive(entry);
    case PartsRole:
        return entry.parts.size();
    default:
        return {};
    }
}

int InstalledModelsModel::sourceIndex(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return -1;
    return m_rows.at(row);
}

int InstalledModelsModel::rowForSourceIndex(int sourceIndex) const
{
    return m_rows.indexOf(sourceIndex);
}

void InstalledModelsModel::setEntries(const QList<ModelEntry> &entries)
{
    // Which rows belong to this role depends on the *active* model, so the
    // filtered set is recomputed here rather than by the caller.
    QList<int> rows;
    for (int i = 0; i < entries.size(); ++i) {
        if (matches(entries.at(i)))
            rows.append(i);
    }
    if (rows == m_rows && entries == m_entries)
        return;
    beginResetModel();
    m_entries = entries;
    m_rows = rows;
    endResetModel();
    emit countChanged();
}

void InstalledModelsModel::settingsChanged()
{
    // The role filter itself can change (activating a model moves it between
    // the recognition and the check list), so this is a rebuild, not a repaint.
    setEntries(m_entries);
}

bool InstalledModelsModel::matches(const ModelEntry &entry) const
{
    return matchesRole(entry, m_settings, m_forCheck);
}

bool InstalledModelsModel::matchesRole(const ModelEntry &entry,
                                       const SettingsStore &settings, bool forCheck)
{
    const bool ocrActive = !entry.modelPath.isEmpty()
                           && entry.modelPath == settings.launchModelPath();
    const bool checkActive = !entry.modelPath.isEmpty()
                             && entry.modelPath == settings.checkLaunchModelPath();

    if (forCheck ? checkActive : ocrActive)
        return true;
    if (!entry.roles.isEmpty())
        return entry.roles.contains(forCheck ? QStringLiteral("check")
                                             : QStringLiteral("ocr"));
    if (checkActive)
        return false;
    const bool vision = !entry.mmprojPath.isEmpty();
    return forCheck ? !vision : vision;
}

bool InstalledModelsModel::isActive(const ModelEntry &entry) const
{
    if (entry.modelPath.isEmpty())
        return false;
    const QString &active = m_forCheck ? m_settings.checkLaunchModelPath()
                                       : m_settings.launchModelPath();
    return entry.modelPath == active;
}

QString InstalledModelsModel::displayTitle(const ModelEntry &entry)
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
