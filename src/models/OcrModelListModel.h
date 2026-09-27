#pragma once

#include <QAbstractListModel>
#include <QString>

namespace llocr {

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

    Q_INVOKABLE int rowOfId(const QString &id) const;

    Q_INVOKABLE QString displayName(const QString &id) const;

private:
    QStringList m_ids;
    QStringList m_names;
};

}  // namespace llocr
