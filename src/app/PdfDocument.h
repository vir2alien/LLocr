#pragma once

#include <QImage>
#include <QMutex>
#include <QSizeF>
#include <QString>

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
    int pageCount() const;
    QSizeF pagePointSize(int index) const;
    QImage render(int index, const QSize &size);

private:
    mutable QRecursiveMutex m_mutex;
    QPdfDocument *m_document = nullptr;
};

}  // namespace llocr
