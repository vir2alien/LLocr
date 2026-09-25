#pragma once

#include <QSortFilterProxyModel>
#include <QString>

namespace llocr {

// Filters a VerificationBlocksModel (or compatible model exposing a "group"
// role) down to a single UI group: content / captions / service.
class BlockGroupFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString group READ group WRITE setGroup NOTIFY groupChanged)

public:
    explicit BlockGroupFilterModel(QObject *parent = nullptr);

    const QString &group() const { return m_group; }
    void setGroup(const QString &group);

signals:
    void groupChanged();

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const override;

private:
    QString m_group;
};

}  // namespace llocr
