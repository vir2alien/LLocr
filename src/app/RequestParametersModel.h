#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/RequestProfile.h"

namespace llocr {

class RequestParametersModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        ValueTextRole,
        KindRole,
        OrderRole,
        DescriptionRole,
    };

    explicit RequestParametersModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<RequestParameter> &parameters() const { return m_parameters; }

    void resetFrom(const QList<RequestParameter> &parameters);
    bool setValue(int row, const QString &text);
    // Appends a custom parameter; the value kind is inferred from the text
    // (number, then boolean, else string). Fails on an empty or duplicate name.
    bool appendRow(const QString &name, const QString &text);

private:
    QList<RequestParameter> m_parameters;
};

}  // namespace llocr
