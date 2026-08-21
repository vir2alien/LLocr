#include "app/AppController.h"
#include "app/PageIndex.h"

#include <algorithm>
#include <utility>

#include <QBuffer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVariantMap>

#include "parsers/DetTokensParser.h"
#include "parsers/ParserFactory.h"

namespace {

bool isPdfPath(const QString& path)
{
    return QFileInfo(path).suffix().toLower() == QStringLiteral("pdf");
}

}  // namespace

namespace llocr {

AppController::AppController(SettingsStore &settings, QObject *parent)
    : m_settings(settings)
    , m_recognition(settings, [this](int index) { return m_document.page(index).image; })
    , QObject(parent)
{
    connect(&m_recognition, &RecognitionController::busyChanged, this, [this]() {
        emit busyChanged();
    });
    connect(&m_recognition, &RecognitionController::statusRequested, this,
            [this](const QString& message) { setStatus(message); });
    connect(&m_recognition, &RecognitionController::rawResultReady, this,
            &AppController::applyRawResult);

    connect(&m_boxModel, &BoxListModel::boxRemoved, this, &AppController::onBoxRemoved);

    connect(this, &AppController::pageChanged, this, [this]() {
        ++m_imageRevision;
        emit imageRevisionChanged();
    });
    connect(this, &AppController::documentChanged, this, [this]() {
        ++m_docRevision;
        emit docRevisionChanged();
        ++m_imageRevision;
        emit imageRevisionChanged();
    });

    // canRecognize() depends on the configured model name; propagate changes
    // so the QML-side enabled-state follows the Settings dialog.
    connect(&m_settings, &SettingsStore::modelNameChanged, this, [this]() {
        emit configChanged();
    });
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
    return m_editStore.effectiveText(m_document, index);
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
    return m_editStore.isEdited(m_currentPage);
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

QImage AppController::croppedImage(int pageIndex, int boxIndex) const
{
    if (!m_document.isValidIndex(pageIndex))
        return {};
    const DocumentPage& page = m_document.page(pageIndex);
    if (!page.recognized || page.result.pages.isEmpty())
        return {};
    const QList<BoundingBox>& boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return {};

    const QRectF norm = boxes.at(boxIndex).rect;
    if (norm.width() <= 0.0 || norm.height() <= 0.0)
        return {};

    QRect px(qRound(norm.x() * page.image.width()),
             qRound(norm.y() * page.image.height()),
             qRound(norm.width() * page.image.width()),
             qRound(norm.height() * page.image.height()));
    px = px.intersected(page.image.rect());
    if (px.width() < 1 || px.height() < 1)
        return {};
    return page.image.copy(px);
}

void AppController::setPrompt(const QString &prompt)
{
    if (m_prompt == prompt)
        return;
    m_prompt = prompt;
    emit promptChanged();
}

void AppController::setCurrentPage(int index)
{
    if (!m_document.isValidIndex(index) || index == m_currentPage)
        return;

    m_currentPage = index;
    m_pageModel.setCurrent(index);
    updateBoxesForCurrent();

    notifyPageChanged();
}

void AppController::updateBoxesForCurrent()
{
    if (m_document.isValidIndex(m_currentPage) && m_document.page(m_currentPage).recognized)
        m_boxModel.setFromResult(m_document.page(m_currentPage).result);
    else
        m_boxModel.setBoxes({});
}

void AppController::openFiles(const QVariantList& fileUrls)
{
    if (m_recognition.busy())
        return;

    QStringList paths;
    for (const QVariant& variant : fileUrls) {
        const QUrl url = variant.toUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!path.isEmpty())
            paths.append(path);
    }

    if (paths.isEmpty()) {
        setStatus(tr("No files selected."));
        return;
    }

    const bool wasEmpty = m_document.isEmpty();
    int addedFiles = 0;
    int addedPages = 0;
    int skipped = 0;

    for (const QString& path : paths) {
        const int pagesBefore = m_document.pageCount();
        const bool ok = isPdfPath(path) ? m_document.appendPdf(path)
                                        : m_document.appendImage(path);
        if (ok) {
            ++addedFiles;
            addedPages += m_document.pageCount() - pagesBefore;
        } else {
            ++skipped;
        }
    }

    if (addedPages == 0) {
        if (wasEmpty)
            setStatus(tr("No supported files selected."));
        else
            setStatus(tr("None of the selected files could be added."));
        return;
    }

    if (wasEmpty) {
        m_currentPage = 0;
    }
    m_pageModel.appendPages(addedPages);
    m_pageModel.setCurrent(m_currentPage);
    updateBoxesForCurrent();

    if (skipped > 0) {
        setStatus(tr("Added %1 file(s), %2 page(s); %3 file(s) skipped.")
                      .arg(addedFiles).arg(addedPages).arg(skipped));
    } else {
        setStatus(tr("Added %1 file(s), %2 page(s).").arg(addedFiles).arg(addedPages));
    }

    notifyDocumentChanged();
}

bool AppController::removePage(int index)
{
    if (m_recognition.busy())
        return false;
    if (!m_document.isValidIndex(index))
        return false;

    m_document.removePage(index);

    if (m_document.isEmpty()) {
        m_editStore.clear();
        m_currentPage = 0;
        m_pageModel.clear();
        m_boxModel.setBoxes({});

        setStatus(tr("Page %1 deleted.").arg(index + 1));

        notifyDocumentChanged();
        return true;
    }

    m_editStore.remapAfterRemove(index);

    if (m_currentPage > index)
        --m_currentPage;
    else if (m_currentPage == index)
        m_currentPage = (std::min)(m_currentPage, m_document.pageCount() - 1);

    m_pageModel.removePage(index);
    m_pageModel.setCurrent(m_currentPage);

    updateBoxesForCurrent();

    setStatus(tr("Page %1 deleted.").arg(index + 1));

    notifyDocumentChanged();
    return true;
}

bool AppController::movePage(int from, int to)
{
    if (m_recognition.busy())
        return false;
    if (!m_document.isValidIndex(from) || !m_document.isValidIndex(to))
        return false;
    if (from == to)
        return true;

    m_document.movePage(from, to);
    m_pageModel.movePage(from, to);

    m_editStore.remapAfterMove(from, to);

    m_currentPage = remapIndexAfterMove(m_currentPage, from, to);

    m_pageModel.setCurrent(m_currentPage);
    updateBoxesForCurrent();

    setStatus(tr("Moved page %1 to position %2.").arg(from + 1).arg(to + 1));

    notifyDocumentChanged();
    return true;
}

void AppController::recognizeCurrent()
{
    if (m_recognition.busy() || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(tr("Set a model name in Settings first."));
        return;
    }

    m_recognition.startCurrent(m_currentPage, m_document.pageCount(), m_prompt);
}

void AppController::recognizeAll()
{
    if (m_recognition.busy() || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(tr("Set a model name in Settings first."));
        return;
    }

    m_recognition.startAll(m_document.pageCount(), m_prompt);
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

    const bool hadDups = !parsed.pages.isEmpty() && parsed.pages.first().hasDuplicates;
    if (hadDups)
        m_pageModel.setHasDuplicates(index, true);

    const bool droppedEdit = m_editStore.revert(index);
    if (droppedEdit)
        m_pageModel.setEdited(index, false);

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
    m_recognition.stop();
}

void AppController::setCurrentPageText(const QString& text)
{
    if (!currentPageEditable())
        return;

    const int index = m_currentPage;
    const QString original = m_document.page(index).result.text;

    switch (m_editStore.setText(index, original, text)) {
    case PageEditStore::Change::NowEdited:
        m_pageModel.setEdited(index, true);
        emit editStateChanged();
        break;
    case PageEditStore::Change::NowClean:
        m_pageModel.setEdited(index, false);
        emit editStateChanged();
        break;
    case PageEditStore::Change::None:
        break;
    }
}

void AppController::revertCurrentPageEdits()
{
    const int index = m_currentPage;
    if (m_editStore.revert(index)) {
        m_pageModel.setEdited(index, false);
        emit resultChanged();
        emit editStateChanged();
    }
}

void AppController::onBoxRectChanged(int boxIndex, qreal x, qreal y,
                                     qreal width, qreal height)
{
    if (!m_document.isValidIndex(m_currentPage))
        return;
    DocumentPage& page = m_document.page(m_currentPage);
    if (!page.recognized || page.result.pages.isEmpty())
        return;
    QList<BoundingBox>& boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return;

    // Update the source-of-truth coordinates. page.text is NOT touched: the
    // image URL still points at the same box index and OcrImageProvider reads
    // the rect lazily, so the crop already reflects the new bounds.
    boxes[boxIndex].rect = QRectF(x, y, width, height);

    // Keep the UI-facing overlay model in sync so the box follows the cursor
    // live during the drag.
    m_boxModel.updateBoxRect(boxIndex, x, y, width, height);

    emit boxesChanged();
    emit imageChanged();
}

void AppController::onBoxRemoved(int boxIndex)
{
    if (!m_document.isValidIndex(m_currentPage))
        return;
    DocumentPage& page = m_document.page(m_currentPage);
    if (!page.recognized || page.result.pages.isEmpty())
        return;
    QList<BoundingBox>& boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return;

    boxes.removeAt(boxIndex);

    // Regenerate the page text so the box indices embedded in the image URLs
    // shift correctly. The result replaces any manual edit, while the original
    // recognition output (page.result.text) is left untouched so Revert
    // restores the text as it was before the removal.
    m_editStore.replace(m_currentPage, rebuildPageText(page.result.pages[0]));
    m_pageModel.setEdited(m_currentPage, true);

    emit boxesChanged();
    emit resultChanged();
    emit editStateChanged();
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
        lo = (std::max)(0, lo);
        hi = (std::min)(m_document.pageCount() - 1, hi);
        break;
    case ExportAll:
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

    // Crops an image-block on demand during export. pageNumber is the 1-based
    // page number carried by Exporter::Page (== document index + 1).
    const auto crop = [this](int pageNumber, int boxIndex) {
        return croppedImage(pageNumber - 1, boxIndex);
    };
    const Exporter::Result result = m_exporter.exportToFile(pages, path, crop);
    setStatus(result.success ? tr("%1 (%2 page(s)).").arg(result.message).arg(pages.size())
                             : result.message);
    return result.success;
}

bool AppController::exportResult(const QUrl& fileUrl)
{
    return exportPages(fileUrl, ExportAll, 1, m_document.pageCount());
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

void AppController::setStatus(const QString& message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusChanged();
}

void AppController::notifyDocumentChanged()
{
    emit documentChanged();
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
}

void AppController::notifyPageChanged()
{
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
}

QString AppController::resolveImagesForPreview(const QString& markdown) const
{
    static const QRegularExpression re(
        QStringLiteral(R"(!\[([^\]]*)\]\(image://ocr/crop/(\d+)\))"));

    QString result;
    int last = 0;
    auto it = re.globalMatch(markdown);

    while (it.hasNext()) {
        const auto m = it.next();
        result += markdown.mid(last, m.capturedStart() - last);

        const int boxIndex = m.captured(2).toInt();
        const QImage img = croppedImage(m_currentPage, boxIndex);
        if (!img.isNull()) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            if (buffer.open(QIODevice::WriteOnly)) {
                img.save(&buffer, "PNG");
                result += QStringLiteral("![%1](data:image/png;base64,%2)")
                              .arg(m.captured(1), QString::fromLatin1(bytes.toBase64()));
            } else {
                result += m.captured();
            }
        } else {
            result += m.captured();
        }
        last = m.capturedEnd();
    }
    result += markdown.mid(last);
    return result;
}

}  // namespace llocr
