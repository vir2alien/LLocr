#pragma once

#include <QAbstractListModel>
#include <QList>

namespace llocr {

class PageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles : int {
        PageIndexRole = Qt::UserRole + 1,
        RecognizedRole,
        CurrentRole,
        EditedRole,
        HasDuplicatesRole,
        RecognitionModeRole,  ///< int PageRecognitionMode — how the result came to be
        VerificationRole,     ///< int PageVerification — the check aggregate
    };

    explicit PageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void appendPages(int count);
    void setRecognized(int index, bool recognized);
    void setEdited(int index, bool edited);
    void setHasDuplicates(int index, bool hasDup);
    void setRecognitionMode(int index, int mode);
    void setVerification(int index, int verification);
    void setCurrent(int index);
    void removePage(int index);
    void movePage(int from, int to);
    void clear();

private:
    QList<bool> m_recognized;
    QList<bool> m_edited;
    QList<bool> m_hasDuplicates;
    QList<int> m_recognitionMode;
    QList<int> m_verification;
    int m_current = -1;
};

}  // namespace llocr
