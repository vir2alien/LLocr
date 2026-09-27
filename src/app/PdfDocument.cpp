#include "app/PdfDocument.h"

#include <QMutexLocker>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>

namespace llocr {

PdfDocument::~PdfDocument()
{
    QMutexLocker locker(&m_mutex);
    delete m_document;
    m_document = nullptr;
}

bool PdfDocument::open(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    if (m_document)
        return true;
    m_document = new QPdfDocument();
    if (m_document->load(path) != QPdfDocument::Error::None) {
        delete m_document;
        m_document = nullptr;
        return false;
    }
    return true;
}

int PdfDocument::pageCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_document ? m_document->pageCount() : 0;
}

QSizeF PdfDocument::pagePointSize(int index) const
{
    QMutexLocker locker(&m_mutex);
    return m_document ? m_document->pagePointSize(index) : QSizeF();
}

QImage PdfDocument::render(int index, const QSize &size)
{
    QMutexLocker locker(&m_mutex);
    if (!m_document)
        return {};
    QPdfDocumentRenderOptions options;
    return m_document->render(index, size, options);
}

}  // namespace llocr
