#include "app/CandidateRenderer.h"

#include <QCoreApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfDocument>
#include <QSizeF>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>

namespace llocr {

namespace {

constexpr int kPollIntervalMs = 100;
constexpr int kMaxPollCount = 40;  // ~4 s cap for the font settling
constexpr int kWatchdogTimeoutMs = 60000;
// The render leg's page width in CSS pixels; the candidate paints its lines at
// roughly the width the training renderer used.
constexpr int kContentWidthPx = 780;
// The raster scale over the CSS pixel size: text must stay crisp after the
// decision model reads the image.
constexpr int kRasterScale = 2;
constexpr int kMaxRasterSide = 4096;
// Chromium's print layout keeps its own margins, so a page cut to the measured
// content height is a hair shorter than the content: the last line falls onto
// a second page, and only the first page is rasterized — the candidate would
// lose its tail without a word (ADR 237). The page gets the room, and the
// raster is cut back to the ink so the extra room stays invisible.
constexpr int kPrintAllowancePx = 48;
constexpr int kInkMarginPx = 2;

// The bounding box of the ink on a white page: the candidate is cut to it, so
// the print allowance and the page padding do not show as a white band around
// the text the model has to read.
QImage cutToInk(const QImage &image)
{
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const int w = gray.width();
    const int h = gray.height();
    int minX = w;
    int minY = h;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < h; ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (row[x] >= 245)
                continue;
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }
    if (maxX < 0)
        return image;
    const QRect ink(QPoint(minX, minY), QPoint(maxX, maxY));
    const QRect padded = ink.adjusted(-kInkMarginPx, -kInkMarginPx, kInkMarginPx, kInkMarginPx);
    return image.copy(padded.intersected(image.rect()));
}

}  // namespace

class CandidateRendererPage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType, bool) override { return url.scheme() == QLatin1String("qrc") && url.path().startsWith(QLatin1String("/preview/")); }
};

CandidateRenderer::CandidateRenderer(QObject *parent) : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &CandidateRenderer::pollFonts);

    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(kWatchdogTimeoutMs);
    connect(m_watchdog, &QTimer::timeout, this, [this]() { fail(tr("The candidate render timed out.")); });
}

CandidateRenderer::~CandidateRenderer() = default;

void CandidateRenderer::render(const QString &text, const ResultCallback &callback)
{
    if (m_busy) {
        if (callback)
            callback(false, {}, tr("The candidate renderer is busy."));
        return;
    }

    m_busy = true;
    m_text = text;
    m_callback = callback;
    m_watchdog->start();
    ensurePage();
}

void CandidateRenderer::ensurePage()
{
    if (!m_page) {
        m_page = std::make_unique<CandidateRendererPage>(this);
        connect(m_page.get(), &QWebEnginePage::loadFinished, this, [this](bool ok) {
            m_pageReady = ok;
            if (!m_busy)
                return;
            if (ok)
                startRender();
            else
                fail(tr("Cannot load the candidate render page."));
        });
        connect(m_page.get(), &QWebEnginePage::renderProcessTerminated, this, [this](QWebEnginePage::RenderProcessTerminationStatus, int) {
            m_pageReady = false;
            if (m_busy)
                fail(tr("The candidate renderer process terminated."));
        });
    }

    if (m_pageReady) {
        startRender();
        return;
    }
    m_page->load(QUrl(QStringLiteral("qrc:/preview/preview.html")));
}

void CandidateRenderer::startRender()
{
    // A fresh flag per render; the poll stops on it once the fonts of the new
    // content are in.
    runJs(QStringLiteral("(function(){ window.__fontsSettled = false;"
                         " document.body.style.width = '%1px';"
                         " return typeof window.render === 'function'; })()")
              .arg(kContentWidthPx),
          [this](const QVariant &ok) {
              if (!m_busy)
                  return;
              if (!ok.toBool()) {
                  fail(tr("Cannot load the candidate render page."));
                  return;
              }
              runJs(QStringLiteral("window.render(%1, {dark: false})").arg(jsonString(m_text)), [this](const QVariant &) {
                  if (!m_busy)
                      return;
                  startFontWait();
              });
          });
}

void CandidateRenderer::startFontWait()
{
    runJs(QStringLiteral("document.fonts.ready.then(function(){ window.__fontsSettled = true; })"), [this](const QVariant &) {
        if (!m_busy)
            return;
        m_pollCount = 0;
        m_pollTimer->start();
    });
}

void CandidateRenderer::pollFonts()
{
    if (++m_pollCount > kMaxPollCount) {
        m_pollTimer->stop();
        measure();
        return;
    }
    runJs(QStringLiteral("window.__fontsSettled === true"), [this](const QVariant &settled) {
        if (!settled.toBool() || !m_busy)
            return;
        m_pollTimer->stop();
        measure();
    });
}

void CandidateRenderer::measure()
{
    runJs(QStringLiteral("(function(){ var el = document.getElementById('content');"
                         " return JSON.stringify({w: Math.max(el.scrollWidth, document.body.scrollWidth),"
                         " h: Math.max(el.scrollHeight, document.body.scrollHeight)}); })()"),
          [this](const QVariant &size) {
              if (!m_busy)
                  return;
              const QJsonDocument doc = QJsonDocument::fromJson(size.toString().toUtf8());
              m_widthPx = doc.object().value(QStringLiteral("w")).toInt();
              m_heightPx = doc.object().value(QStringLiteral("h")).toInt();
              if (m_widthPx <= 0 || m_heightPx <= 0) {
                  fail(tr("The candidate render produced an empty page."));
                  return;
              }
              startPrint();
          });
}

void CandidateRenderer::startPrint()
{
    if (!m_temp) {
        m_temp = std::make_unique<QTemporaryDir>();
        if (!m_temp->isValid()) {
            fail(tr("The candidate render could not create a temporary file."));
            return;
        }
    }
    const QString path = m_temp->filePath(QStringLiteral("candidate.pdf"));
    const QSizeF sizePt(m_widthPx * 72.0 / 96.0, (m_heightPx + kPrintAllowancePx) * 72.0 / 96.0);
    const QPageLayout layout(QPageSize(sizePt, QPageSize::Point, QStringLiteral("candidate"), QPageSize::ExactMatch),
                             QPageLayout::Portrait,
                             QMarginsF(0, 0, 0, 0),
                             QPageLayout::Millimeter);
    connect(m_page.get(),
            &QWebEnginePage::pdfPrintingFinished,
            this,
            [this, path](const QString &, bool ok) {
                if (!m_busy)
                    return;
                if (ok) {
                    m_pdfPath = path;
                    rasterize();
                } else {
                    fail(tr("PDF printing failed."));
                }
            },
            Qt::SingleShotConnection);
    m_page->printToPdf(path, layout);
}

void CandidateRenderer::rasterize()
{
    QPdfDocument pdf;
    if (pdf.load(m_pdfPath) != QPdfDocument::Error::None || pdf.pageCount() < 1) {
        fail(tr("The candidate render could not be rasterized."));
        return;
    }
    // A second page means the content did not fit: the picture would carry
    // only its head, and the model would judge a candidate that was never
    // sent (ADR 237).
    if (pdf.pageCount() > 1) {
        fail(tr("The candidate render did not fit on one page."));
        return;
    }
    const QSize size(qMin(m_widthPx * kRasterScale, kMaxRasterSide), qMin((m_heightPx + kPrintAllowancePx) * kRasterScale, kMaxRasterSide));
    const QImage image = cutToInk(pdf.render(0, size));
    finish(!image.isNull(), image, image.isNull() ? tr("The candidate render could not be rasterized.") : QString());
}

void CandidateRenderer::runJs(const QString &script, const std::function<void(const QVariant &)> &onResult)
{
    m_page->runJavaScript(script, [onResult](const QVariant &result) { onResult(result); });
}

QString CandidateRenderer::jsonString(const QString &value)
{
    const QJsonArray arr{value};
    const QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return json.mid(1, json.size() - 2);  // strip the wrapping brackets
}

void CandidateRenderer::fail(const QString &error)
{
    finish(false, {}, error);
}

void CandidateRenderer::finish(bool success, const QImage &image, const QString &error)
{
    m_pollTimer->stop();
    m_watchdog->stop();
    m_busy = false;
    m_pdfPath.clear();

    const ResultCallback callback = std::move(m_callback);
    m_callback = {};
    if (callback)
        callback(success, success ? image : QImage(), success ? QString() : error);
}

}  // namespace llocr
