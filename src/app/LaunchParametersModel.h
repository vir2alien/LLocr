#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/LaunchProfile.h"

namespace llocr {

/// List model behind the Launch settings table: one row per command-line
/// parameter of the draft (unsaved) copy of the selected launch profile.
/// Rows can be edited, added and removed; nothing is persisted until
/// LaunchProfileStore::saveDraft().
class LaunchParametersModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        ValueTextRole,
        KindRole,
        DescriptionRole,
    };

    explicit LaunchParametersModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<LaunchParameter> &parameters() const { return m_parameters; }

    /// Replaces the draft wholesale (model reset).
    void resetFrom(const QList<LaunchParameter> &parameters);

    /// Applies a free-text edit to one row. An emptied value turns the row
    /// into a flag, a filled flag row becomes a Number/Text row; a Number row
    /// only accepts a finite number literal. Returns false when the edit is
    /// rejected (row untouched — the QML side reverts the editor text).
    bool setValue(int row, const QString &text);

    /// Appends a row (empty value → flag, numeric text → Number, otherwise
    /// Text). The name is trimmed and stripped of leading dashes; empty,
    /// reserved (model/mmproj/alias/host/port) or duplicate names are refused.
    bool appendRow(const QString &name, const QString &text);

    /// Removes one row (no-op on a bad index).
    void removeRow(int row);

private:
    QList<LaunchParameter> m_parameters;
};

}  // namespace llocr
