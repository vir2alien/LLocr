#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/RequestProfile.h"

namespace llocr {

/// List model behind the Request settings table: one row per request-profile
/// parameter, in profile order. Holds the *draft* (unsaved) parameter values;
/// edits stay here until RequestProfileStore::saveDraft() commits them to the
/// user profile.
class RequestParametersModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        ValueTextRole,
        KindRole,
        OrderRole,
    };

    explicit RequestParametersModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<RequestParameter> &parameters() const { return m_parameters; }

    /// Replaces the draft wholesale (model reset).
    void resetFrom(const QList<RequestParameter> &parameters);

    /// Parses `text` strictly by the row's value kind; on success updates the
    /// row (dataChanged) and returns true. On a format mismatch returns false
    /// and leaves the row untouched — the QML side reverts the editor text.
    bool setValue(int row, const QString &text);

private:
    QList<RequestParameter> m_parameters;
};

}  // namespace llocr
