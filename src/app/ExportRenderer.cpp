#include "app/ExportRenderer.h"

#include "app/Exporter.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>

namespace llocr {

class ExportRenderer::ExportPage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType, bool) override
    {
        return url.scheme() == QLatin1String("qrc")
            && url.path().startsWith(QLatin1String("/preview/"));
    }
};

namespace {
constexpr int kPollIntervalMs = 100;
constexpr int kMaxPollCount = 40;    // ~4 s cap for the font settling
constexpr int kWatchdogTimeoutMs = 180000;
}  // namespace

ExportRenderer::ExportRenderer(QObject *parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &ExportRenderer::pollFonts);

    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(kWatchdogTimeoutMs);
    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        fail(tr("Export rendering timed out."));
    });
}

ExportRenderer::~ExportRenderer() = default;

void ExportRenderer::render(const Request &request, const ResultCallback &callback)
{
    if (m_busy) {
        if (callback)
            callback(false, {}, tr("The export renderer is busy."));
        return;
    }
    if (request.pages.isEmpty()) {
        if (callback)
            callback(false, {}, QCoreApplication::translate("Exporter", "Nothing to export."));
        return;
    }

    m_busy = true;
    m_output = request.output;
    m_pages = request.pages;
    m_nextPage = 0;
    m_styleSheet = request.styleSheet;
    m_splitPages = request.splitPages;
    m_pageLayout = request.pageLayout.isValid()
                       ? request.pageLayout : Exporter::defaultPdfLayout();
    m_outputPath = request.outputPath;
    m_callback = std::move(callback);
    m_printing = false;

    m_watchdog->start();
    ensurePage();
}

void ExportRenderer::ensurePage()
{
    if (!m_page) {
        m_page = std::make_unique<ExportPage>(this);
        connect(m_page.get(), &QWebEnginePage::loadFinished, this,
                [this](bool ok) {
            m_pageReady = ok;
            if (!m_busy)
                return;
            if (ok)
                startRun();
            else
                fail(tr("Cannot load the export page."));
        });
        connect(m_page.get(), &QWebEnginePage::renderProcessTerminated, this,
                [this](QWebEnginePage::RenderProcessTerminationStatus, int) {
            m_pageReady = false;
            if (m_busy)
                fail(tr("The export renderer process terminated."));
        });
    }

    if (m_pageReady) {
        startRun();
        return;
    }
    m_page->load(QUrl(QStringLiteral("qrc:/preview/export.html")));
}

void ExportRenderer::startRun()
{
    runJs(QStringLiteral("typeof window.beginExport === 'function'"
                        " && window.beginExport(%1, %2)")
              .arg(jsonString(m_styleSheet), m_splitPages ? QStringLiteral("true")
                                                          : QStringLiteral("false")),
          [this](const QVariant &ok) {
        if (!m_busy)
            return;
        if (!ok.toBool()) {
            fail(tr("Cannot load the export page."));
            return;
        }
        appendNextPage();
    });
}

void ExportRenderer::appendNextPage()
{
    if (!m_busy)
        return;
    if (m_nextPage >= m_pages.size()) {
        startFontWait();
        return;
    }

    const PageInput page = m_pages.at(m_nextPage);
    emit progress(m_nextPage, m_pages.size());

    runJs(QStringLiteral("typeof window.appendExportPage === 'function'"
                        " && window.appendExportPage(%1, %2)")
              .arg(QString::number(page.first), jsonString(page.second)),
          [this](const QVariant &ok) {
        if (!m_busy)
            return;
        if (!ok.toBool()) {
            fail(tr("Cannot load the export page."));
            return;
        }
        ++m_nextPage;
        appendNextPage();
    });
}

void ExportRenderer::startFontWait()
{
    emit progress(m_pages.size(), m_pages.size());

    runJs(QStringLiteral("typeof window.finishExport === 'function' && window.finishExport()"),
          [this](const QVariant &ok) {
        if (!m_busy)
            return;
        if (!ok.toBool()) {
            fail(tr("Cannot load the export page."));
            return;
        }
        m_pollCount = 0;
        m_pollTimer->start();
    });
}

void ExportRenderer::pollFonts()
{
    if (++m_pollCount > kMaxPollCount) {
        m_pollTimer->stop();
        deliver();
        return;
    }
    runJs(QStringLiteral("window.__fontsSettled === true"), [this](const QVariant &settled) {
        if (!settled.toBool())
            return;
        if (!m_busy)
            return;
        m_pollTimer->stop();
        deliver();
    });
}

void ExportRenderer::deliver()
{
    if (m_output == Output::Html) {
        runJs(QStringLiteral("document.getElementById('export-root').innerHTML"),
              [this](const QVariant &html) {
            finish(true, html.toString(), {});
        });
        return;
    }
    startPdfPrint();
}

void ExportRenderer::startPdfPrint()
{
    if (m_printing)
        return;
    m_printing = true;
    connect(m_page.get(), &QWebEnginePage::pdfPrintingFinished, this,
            [this](const QString &, bool ok) {
        m_printing = false;
        if (!m_busy)
            return;
        if (ok)
            finish(true, {}, {});
        else
            fail(tr("PDF printing failed."));
    }, Qt::SingleShotConnection);

    m_page->printToPdf(m_outputPath, m_pageLayout);
}

void ExportRenderer::runJs(const QString &script,
                           const std::function<void(const QVariant &)> &onResult)
{
    m_page->runJavaScript(script, [this, onResult](const QVariant &result) {
        onResult(result);
    });
}

QString ExportRenderer::jsonString(const QString &value)
{
    const QJsonArray arr{ value };
    const QString json =
        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return json.mid(1, json.size() - 2);  // strip the wrapping brackets
}

void ExportRenderer::fail(const QString &error)
{
    finish(false, {}, error);
}

void ExportRenderer::finish(bool success, const QString &html, const QString &error)
{
    m_pollTimer->stop();
    m_watchdog->stop();
    m_busy = false;
    m_printing = false;

    const ResultCallback callback = std::move(m_callback);
    m_callback = {};
    if (callback)
        callback(success, success ? html : QString(), success ? QString() : error);
}

} // namespace llocr
