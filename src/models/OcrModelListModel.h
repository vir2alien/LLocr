#pragma once

#include <QAbstractListModel>
#include <QString>

namespace llocr {

/// The OCR model adapters as a QML-facing list: one row per registered model,
/// carrying both its id and its display name.
///
/// The UI used to round-trip through display names (`Controller.modelNames` +
/// `modelIdToName`/`modelNameToId`), which silently substituted the default
/// model for an unknown name — a combo could show one model while recognition
/// used another (ADR 110). With the id as a role there is nothing to lose.
class OcrModelListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        DisplayRole,
    };

    explicit OcrModelListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Row of a model id, or -1 when it is not registered.
    Q_INVOKABLE int rowOfId(const QString &id) const;

    /// Display name of a model id, empty when it is not registered.
    Q_INVOKABLE QString displayName(const QString &id) const;

private:
    QStringList m_ids;
    QStringList m_names;
};

}  // namespace llocr
