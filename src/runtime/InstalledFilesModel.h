#pragma once

#include <QAbstractListModel>
#include <QList>

#include "runtime/ModelRegistry.h"

namespace llocr {

class ModelInstaller;

// The "files on disk" block of the Models window (ADR 168): one row per
// installed registry entry — a model+quant pair — with its size, how many
// tasks use it, and whether it may be deleted.
class InstalledFilesModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles : int {
        EntryIdRole = Qt::UserRole + 1,
        TitleRole,
        QuantRole,
        SizeBytesRole,
        SizeLabelRole,
        UsageCountRole,
        UsageTextRole,
        RemovableRole,
        NoteRole,
    };
    Q_ENUM(Roles)

    explicit InstalledFilesModel(ModelInstaller &installer, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSource(QList<ModelEntry> entries);
    // Usage counts and removal guards changed (settings or runtime state);
    // the entry list itself is unchanged.
    void refresh();

private:
    ModelInstaller &m_installer;
    QList<ModelEntry> m_entries;
};

}  // namespace llocr
