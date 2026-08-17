#include "app/AppController.h"

#include <utility>

#include <QFileInfo>
#include <QVariantMap>

#include "parsers/ParserFactory.h"

namespace llocr {

AppController::AppController(SettingsStore &settings, QObject *parent)
    : m_settings(settings), QObject(parent)
{
    m_provider = std::make_unique<OpenAiProvider>();

    connect(&m_watcher, &QFutureWatcher<OcrResult>::finished, this, &AppController::onRecognitionFinished);
}

QStringList AppController::parserNames() const
{
    return ParserFactory::registeredIds();
}

bool AppController::hasImage() const
{
    return !m_document.isEmpty();
}

bool AppController::hasResult() const
{
    for (int i = 0; i < m_document.pageCount(); ++i) {
        if (m_document.page(i).recognized)
            return true;
    }
    return false;
}

bool AppController::canRecognize() const
{
    return !m_settings.modelName().trimmed().isEmpty();
}

QString AppController::effectiveText(int index) const
{
    if (!m_document.isValidIndex(index))
        return {};
    const DocumentPage& page = m_document.page(index);
    if (!page.recognized)
        return {};
    const auto it = m_edits.constFind(index);
    if (it != m_edits.constEnd())
        return it.value();
    return page.result.text;
}

QString AppController::resultText() const
{
    return effectiveText(m_currentPage);
}

bool AppController::currentPageEditable() const
{
    return m_document.isValidIndex(m_currentPage) && m_document.page(m_currentPage).recognized;
}

bool AppController::currentPageEdited() const
{
    return m_edits.contains(m_currentPage);
}

QImage AppController::currentImage() const
{
    return pageImage(m_currentPage);
}

QImage AppController::pageImage(int index) const
{
    if (!m_document.isValidIndex(index))
        return {};
    return m_document.page(index).image;
}

OcrResult AppController::currentResult() const
{
    if (!m_document.isValidIndex(m_currentPage))
        return {};
    return m_document.page(m_currentPage).result;
}

void AppController::setPrompt(const QString &prompt)
{
    if (m_prompt == prompt)
        return;
    m_prompt = prompt;
    emit promtChanged();
}

void AppController::setCurrentPage(int index)
{
    if (!m_document.isValidIndex(index) || index == m_currentPage)
        return;

    m_currentPage = index;
    m_pageModel.setCurrent(index);
    updateBoxesForCurrent();

    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
}

void AppController::updateBoxesForCurrent()
{
    if (m_document.isValidIndex(m_currentPage) && m_document.page(m_currentPage).recognized)
        m_boxModel.setFromResult(m_document.page(m_currentPage).result);
    else
        m_boxModel.setBoxes({});
}

bool AppController::openImage(const QString& filePath)
{
    if (m_busy)
        return false;

    if (!m_document.loadImage(filePath)) {
        setStatus(tr("Failed to open image: %1").arg(filePath));
        return false;
    }

    m_edits.clear();
    m_currentPage = 0;
    m_pageModel.setPageCount(m_document.pageCount());
    m_boxModel.setBoxes({});

    setStatus(tr("Opened %1").arg(QFileInfo(filePath).fileName()));

    emit documentChanged();
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
    return true;
}

bool AppController::openImages(const QStringList& filePaths)
{
    if (m_busy)
        return false;

    if (filePaths.isEmpty())
        return false;

    if (!m_document.loadImages(filePaths)) {
        setStatus(tr("Failed to open images."));
        return false;
    }

    m_edits.clear();
    m_currentPage = 0;
    m_pageModel.setPageCount(m_document.pageCount());
    m_boxModel.setBoxes({});

    setStatus(tr("Opened %1 image(s)").arg(m_document.pageCount()));

    emit documentChanged();
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
    return true;
}

void AppController::openFiles(const QVariantList& fileUrls)
{
    if (m_busy)
        return;

    QStringList images;
    QStringList documents;

    for (const QVariant& variant : fileUrls) {
        const QUrl url = variant.toUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (path.isEmpty())
            continue;

        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QStringLiteral("pdf"))
            documents.append(path);
        else
            images.append(path);
    }

    if (!documents.isEmpty()) {
        openDocument(QUrl::fromLocalFile(documents.first()));
        return;
    }

    if (!images.isEmpty()) {
        openImages(images);
        return;
    }

    setStatus(tr("No supported files selected."));
}

void AppController::openDocument(const QUrl& fileUrl)
{
    if (m_busy)
        return;

    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const QString suffix = QFileInfo(path).suffix().toLower();

    bool ok = false;
    if (suffix == QStringLiteral("pdf"))
        ok = m_document.loadPdf(path);
    else
        ok = m_document.loadImage(path);

    if (!ok) {
        setStatus(tr("Failed to open: %1").arg(path));
        return;
    }

    m_edits.clear();
    m_currentPage = 0;
    m_pageModel.setPageCount(m_document.pageCount());
    m_boxModel.setBoxes({});

    setStatus(
        tr("Opened %1 (%2 page(s))").arg(QFileInfo(path).fileName()).arg(m_document.pageCount()));

    emit documentChanged();
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
}

bool AppController::removePage(int index)
{
    if (m_busy)
        return false;
    if (!m_document.isValidIndex(index))
        return false;

    m_document.removePage(index);

    if (m_document.isEmpty()) {
        m_edits.clear();
        m_currentPage = 0;
        m_pageModel.clear();
        m_boxModel.setBoxes({});

        setStatus(tr("Page %1 deleted.").arg(index + 1));

        emit documentChanged();
        emit pageChanged();
        emit imageChanged();
        emit resultChanged();
        emit boxesChanged();
        emit editStateChanged();
        return true;
    }

    QHash<int, QString> shifted;
    shifted.reserve(m_edits.size());
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
        if (it.key() == index)
            continue;
        shifted.insert(it.key() > index ? it.key() - 1 : it.key(), it.value());
    }
    m_edits = shifted;

    if (m_currentPage > index)
        --m_currentPage;
    else if (m_currentPage == index)
        m_currentPage = qMin(m_currentPage, m_document.pageCount() - 1);

    m_pageModel.removePage(index);
    m_pageModel.setCurrent(m_currentPage);

    updateBoxesForCurrent();

    setStatus(tr("Page %1 deleted.").arg(index + 1));

    emit documentChanged();
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
    return true;
}

void AppController::recognizeCurrent()
{
    if (m_busy || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(tr("Set a model name in Settings first."));
        return;
    }

    m_stopRequested = false;
    m_recognizeAll = false;
    setBusy(true);
    recognizePage(m_currentPage);
}

void AppController::recognizeAll()
{
    if (m_busy || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(tr("Set a model name in Settings first."));
        return;
    }

    m_stopRequested = false;
    m_recognizeAll = true;
    setBusy(true);
    recognizeSequential(0);
}

void AppController::recognizeSequential(int index)
{
    if (!m_document.isValidIndex(index)) {
        setStatus(tr("Done."));
        finishRun();
        return;
    }
    recognizePage(index);
}

void AppController::recognizePage(int index)
{
    if (!m_document.isValidIndex(index)) {
        finishRun();
        return;
    }

    m_recognizingIndex = index;
    setStatus(tr("Recognizing page %1 of %2…").arg(index + 1).arg(m_document.pageCount()));

    const OcrRequest request = buildRequest(m_document.page(index).image, m_prompt);

    ProviderConfig config;
    config.apiKey = m_settings.apiKey();
    config.baseUrl = m_settings.baseUrl();
    config.maxTokens = m_settings.maxTokens();
    config.modelName = m_settings.modelName();
    config.parserId = m_settings.parserId();
    config.prompt = m_prompt;
    config.temperature = m_settings.temperature();
    config.timeoutMs = m_settings.connectionTimeoutMs();
    m_watcher.setFuture(m_provider->recognize(request, config));
}

OcrRequest AppController::buildRequest(const QImage &image, const QString &prompt) const
{
    OcrRequest request;
    request.image = image;
    request.prompt = prompt;
    request.modelId = m_settings.modelName();
    request.temperature = m_settings.temperature();
    request.maxTokens = m_settings.maxTokens();
    return request;
}

void AppController::onRecognitionFinished()
{
    if (m_recognizingIndex < 0)
        return;

    const OcrResult raw = m_watcher.future().resultCount() > 0
                              ? m_watcher.result()
                              : OcrResult::makeError(tr("No response"));
    const int index = m_recognizingIndex;

    if (!raw.success) {
        if (m_stopRequested)
            setStatus(tr("Stopped at page %1.").arg(index + 1));
        else {
            setStatus(tr("Error on page %1: %2").arg(index + 1).arg(raw.errorMessage));
            qDebug() << tr("Error on page %1: %2").arg(index + 1).arg(raw.errorMessage);
        }
        finishRun();
        return;
    }

    applyRawResult(index, raw);

    if (m_stopRequested) {
        setStatus(tr("Stopped after page %1.").arg(index + 1));
        finishRun();
        return;
    }

    if (m_recognizeAll) {
        const int next = index + 1;
        if (m_document.isValidIndex(next)) {
            recognizeSequential(next);
            return;
        }
    }

    setStatus(tr("Done."));
    finishRun();
}

void AppController::applyRawResult(int index, const OcrResult& rawResult)
{
    if (!m_document.isValidIndex(index))
        return;

    OcrResult parsed = rawResult;
    if (auto parser = ParserFactory::create(m_settings.parserId())) {
        parsed = parser->parse(rawResult.text);
    }

    DocumentPage& page = m_document.page(index);
    page.result = parsed;
    page.recognized = true;

    m_pageModel.setRecognized(index, true);

    const bool droppedEdit = m_edits.remove(index) > 0;
    if (droppedEdit)
        m_pageModel.setEdited(index, false);

    emit recognitionFinished(parsed);

    if (index == m_currentPage) {
        updateBoxesForCurrent();
        emit resultChanged();
        emit boxesChanged();
        if (droppedEdit)
            emit editStateChanged();
    } else {
        emit resultChanged();
    }
}

void AppController::stop()
{
    if (!m_busy)
        return;
    m_stopRequested = true;
    if (m_provider)
        m_provider->abort();
    setStatus(tr("Stopping…"));
}

void AppController::finishRun()
{
    m_recognizingIndex = -1;
    m_recognizeAll = false;
    setBusy(false);
}

void AppController::setCurrentPageText(const QString& text)
{
    if (!currentPageEditable())
        return;

    const int index = m_currentPage;
    const QString original = m_document.page(index).result.text;

    if (text == original) {
        if (m_edits.remove(index) > 0) {
            m_pageModel.setEdited(index, false);
            emit editStateChanged();
        }
        return;
    }

    const bool wasEdited = m_edits.contains(index);
    m_edits.insert(index, text);
    if (!wasEdited) {
        m_pageModel.setEdited(index, true);
        emit editStateChanged();
    }
}

void AppController::revertCurrentPageEdits()
{
    const int index = m_currentPage;
    if (m_edits.remove(index) > 0) {
        m_pageModel.setEdited(index, false);
        emit resultChanged();
        emit editStateChanged();
    }
}

QList<Exporter::Page> AppController::collectPages(int scope, int fromPage, int toPage) const
{
    int lo = 0;
    int hi = m_document.pageCount() - 1;

    switch (scope) {
    case ExportCurrent:
        lo = hi = m_currentPage;
        break;
    case ExportRange:
        lo = fromPage - 1;
        hi = toPage - 1;
        if (lo > hi)
            std::swap(lo, hi);
        lo = qMax(0, lo);
        hi = qMin(m_document.pageCount() - 1, hi);
        break;
    case ExportAll:
    default:
        break;
    }

    QList<Exporter::Page> pages;
    for (int i = lo; i <= hi; ++i) {
        if (!m_document.isValidIndex(i) || !m_document.page(i).recognized)
            continue;
        Exporter::Page p;
        p.number = i + 1;
        p.text = effectiveText(i);
        pages.append(p);
    }
    return pages;
}

QList<Exporter::Page> AppController::collectRecognizedPages() const
{
    return collectPages(ExportAll, 1, m_document.pageCount());
}

bool AppController::exportPages(const QUrl& fileUrl, int scope, int fromPage, int toPage)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        setStatus(tr("No output path."));
        return false;
    }

    const QList<Exporter::Page> pages = collectPages(scope, fromPage, toPage);
    if (pages.isEmpty()) {
        setStatus(tr("Nothing to export for the selected pages "
                     "(no recognized pages in that selection)."));
        return false;
    }

    const Exporter::Result result = m_exporter.exportToFile(pages, path);
    setStatus(result.success ? tr("%1 (%2 page(s)).").arg(result.message).arg(pages.size())
                             : result.message);
    return result.success;
}

bool AppController::exportResult(const QUrl& fileUrl)
{
    return exportPages(fileUrl, ExportAll, 1, m_document.pageCount());
}

bool AppController::pandocAvailable() const
{
    return Exporter::isPandocAvailable();
}

QStringList AppController::exportNameFilters() const
{
    QStringList filters;
    filters << tr("Markdown (*.md)") << tr("Plain text (*.txt)") << tr("HTML (*.html)");
    if (Exporter::isPandocAvailable())
        filters << tr("Word document (*.docx)");
    filters << tr("PDF (*.pdf)");
    return filters;
}

void AppController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void AppController::setStatus(const QString& message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusChanged();
}

}  // namespace llocr
