#include "app/PdfDocument.h"

#include <QMutexLocker>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QThread>

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
    m_owner = QThread::currentThread();
    if (m_document->load(path) != QPdfDocument::Error::None) {
        delete m_document;
        m_document = nullptr;
        return false;
    }
    return true;
}

bool PdfDocument::ownedByCurrentThread() const
{
    QMutexLocker locker(&m_mutex);
    return m_owner == QThread::currentThread();
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
