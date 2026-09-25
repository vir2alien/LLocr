#include "app/DjVuDocument.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <libdjvu/ddjvuapi.h>
#include <cmath>
#include <memory>

namespace llocr {
namespace {
constexpr qint64 kDecodeTimeoutMs = 30000;
constexpr qint64 kMaxRenderPixels = 40000000;
constexpr int kMaxRenderSide = 16384;

struct PageDeleter {
    void operator()(ddjvu_page_t* page) const { ddjvu_page_release(page); }
};
using PagePtr = std::unique_ptr<ddjvu_page_t, PageDeleter>;
using FormatPtr = std::unique_ptr<ddjvu_format_t, decltype(&ddjvu_format_release)>;
} // namespace

DjVuDocument::~DjVuDocument()
{
    close();
}

void DjVuDocument::close()
{
    if (m_document) {
        ddjvu_job_stop(ddjvu_document_job(m_document));
        ddjvu_document_release(m_document);
        m_document = nullptr;
    }
    if (m_context) {
        ddjvu_context_release(m_context);
        m_context = nullptr;
    }
}

void DjVuDocument::drainMessages()
{
    while (const ddjvu_message_t* message = ddjvu_message_peek(m_context)) {
        if (message->m_any.tag == DDJVU_ERROR && message->m_error.message)
            m_error = QString::fromUtf8(message->m_error.message);
        ddjvu_message_pop(m_context);
    }
}

bool DjVuDocument::waitForJob(ddjvu_job_s* job)
{
    QElapsedTimer timer;
    timer.start();
    while (!ddjvu_job_done(job)) {
        drainMessages();
        if (timer.elapsed() >= kDecodeTimeoutMs) {
            ddjvu_job_stop(job);
            m_error = tr("DjVu decoding timed out.");
            return false;
        }
        QThread::msleep(5);
    }
    drainMessages();
    if (ddjvu_job_error(job)) {
        if (m_error.isEmpty())
            m_error = tr("DjVu decoding failed.");
        return false;
    }
    return true;
}

void DjVuDocument::reportError(QString* error, int index) const
{
    if (!error)
        return;
    const QString detail = m_error.isEmpty() ? tr("DjVu decoding failed.") : m_error;
    *error = index < 0 ? tr("Failed to open DjVu %1: %2").arg(m_path, detail)
                       : tr("Failed to read DjVu %1, page %2: %3")
                             .arg(m_path, QString::number(qint64(index) + 1), detail);
}

bool DjVuDocument::open(const QString& path, QString* error)
{
    close();
    m_path = path;
    m_error.clear();
    if (error)
        error->clear();
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable()) {
        m_error = tr("File does not exist or is not readable.");
        reportError(error);
        return false;
    }
    m_context = ddjvu_context_create("LLocr");
    if (m_context) {
        ddjvu_cache_set_size(m_context, 32ul * 1024 * 1024);
        const QByteArray filename = file.absoluteFilePath().toUtf8();
        m_document = ddjvu_document_create_by_filename_utf8(m_context, filename.constData(), 1);
    }
    if (!m_document || !waitForJob(ddjvu_document_job(m_document)) || pageCount() <= 0) {
        reportError(error);
        close();
        return false;
    }
    return true;
}

int DjVuDocument::pageCount() const
{
    return m_document ? ddjvu_document_get_pagenum(m_document) : 0;
}

QSize DjVuDocument::pageSize(int index, QString* error)
{
    if (error)
        error->clear();
    m_error.clear();
    if (index < 0 || index >= pageCount()) {
        m_error = tr("Invalid DjVu page index.");
        reportError(error, index);
        return {};
    }
    ddjvu_pageinfo_t info = {};
    QElapsedTimer timer;
    timer.start();
    ddjvu_status_t status;
    while ((status = ddjvu_document_get_pageinfo(m_document, index, &info)) < DDJVU_JOB_OK) {
        drainMessages();
        if (timer.elapsed() >= kDecodeTimeoutMs) {
            m_error = tr("DjVu decoding timed out.");
            reportError(error, index);
            return {};
        }
        QThread::msleep(5);
    }
    drainMessages();
    if (status != DDJVU_JOB_OK || info.width <= 0 || info.height <= 0) {
        reportError(error, index);
        return {};
    }
    QSize size(info.width, info.height);
    if (size.width() > kMaxRenderSide || size.height() > kMaxRenderSide)
        size.scale(kMaxRenderSide, kMaxRenderSide, Qt::KeepAspectRatio);
    if (qint64(size.width()) * size.height() > kMaxRenderPixels) {
        const double scale = std::sqrt(double(kMaxRenderPixels)
                                      / (double(size.width()) * size.height()));
        size = QSize(qMax(1, int(size.width() * scale)), qMax(1, int(size.height() * scale)));
    }
    return size;
}

QImage DjVuDocument::render(int index, const QSize& size, QString* error)
{
    if (error)
        error->clear();
    m_error.clear();
    if (index < 0 || index >= pageCount() || size.isEmpty()
        || size.width() > kMaxRenderSide || size.height() > kMaxRenderSide
        || qint64(size.width()) * size.height() > kMaxRenderPixels) {
        m_error = tr("Invalid DjVu page or render size.");
        reportError(error, index);
        return {};
    }
    PagePtr page(ddjvu_page_create_by_pageno(m_document, index));
    if (!page || !waitForJob(ddjvu_page_job(page.get()))) {
        reportError(error, index);
        return {};
    }
    FormatPtr format(ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, nullptr), ddjvu_format_release);
    QImage image(size, QImage::Format_RGB888);
    if (!format || image.isNull()) {
        m_error = tr("Unable to allocate the DjVu image buffer.");
        reportError(error, index);
        return {};
    }
    ddjvu_format_set_row_order(format.get(), 1);
    ddjvu_format_set_y_direction(format.get(), 1);
    const ddjvu_rect_t rect = {0, 0, static_cast<unsigned>(size.width()),
                              static_cast<unsigned>(size.height())};
    const bool ok = ddjvu_page_render(page.get(), DDJVU_RENDER_COLOR, &rect, &rect,
                                     format.get(), static_cast<unsigned long>(image.bytesPerLine()),
                                     reinterpret_cast<char*>(image.bits()));
    drainMessages();
    if (!ok) {
        reportError(error, index);
        return {};
    }
    return image;
}

} // namespace llocr
