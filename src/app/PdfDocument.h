#pragma once

#include <QImage>
#include <QMutex>
#include <QSizeF>
#include <QString>

class QThread;

class QPdfDocument;
class QPdfDocumentRenderOptions;

namespace llocr {

class PdfDocument final
{
public:
    PdfDocument() = default;
    ~PdfDocument();
    Q_DISABLE_COPY_MOVE(PdfDocument)

    bool open(const QString &path);
    // The QPdfDocument is a QObject with the affinity of the thread that
    // created it: only that thread may render with it (ADR 209).
    bool ownedByCurrentThread() const;
    int pageCount() const;
    QSizeF pagePointSize(int index) const;
    QImage render(int index, const QSize &size);

private:
    mutable QRecursiveMutex m_mutex;
    QPdfDocument *m_document = nullptr;
    QThread *m_owner = nullptr;
};

}  // namespace llocr
