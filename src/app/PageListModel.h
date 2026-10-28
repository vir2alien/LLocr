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
        RecognitionModeRole,     ///< int PageRecognitionMode — how the result came to be
        VerificationRole,        ///< int PageVerification — the check aggregate
        OrientationProblemRole,  ///< int PageOrientationProblem — what the orientation check found
        SelectedRole,
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
    void setOrientationProblem(int index, int problem);
    void setCurrent(int index);
    void removePage(int index);
    void movePage(int from, int to);
    void clear();

    // An empty selection means the whole document; a fresh document selects
    // its current page so the toolbar scope starts at one page.
    Q_INVOKABLE void selectSingle(int index);
    Q_INVOKABLE void setSelected(int index, bool selected);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE int selectedCount() const;
    Q_INVOKABLE bool isSelected(int index) const;

    // The toolbar's page-range field edits the selection through this pair:
    // the compact 1-based form ("1-10, 15"), an empty selection formats to an
    // empty string. applyPagesRange returns the parsed page count; an empty
    // input clears the selection (the whole document), an input with no valid
    // token changes nothing.
    Q_INVOKABLE QString selectedPagesRange() const;
    Q_INVOKABLE int applyPagesRange(const QString &range);
    static QString formatPageRange(QList<int> pages);
    static QList<int> parsePageRange(const QString &range, int pageCount);

signals:
    void selectionChanged();

private:
    QList<bool> m_recognized;
    QList<bool> m_edited;
    QList<bool> m_hasDuplicates;
    QList<int> m_recognitionMode;
    QList<int> m_verification;
    QList<int> m_orientationProblem;
    QList<bool> m_selected;
    int m_current = -1;
};

}  // namespace llocr
