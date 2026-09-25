#pragma once

#include <QSortFilterProxyModel>
#include <QString>

namespace llocr {

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
