#include "app/BlockInspector.h"
#include "app/BlockTextHighlighter.h"
#include "app/DocumentModel.h"

#include <QQuickTextDocument>
#include <QTextDocument>

namespace llocr {

BlockInspector::BlockInspector(Deps deps, QObject *parent) : QObject(parent), m_deps(deps)
{
    // The panel's diff view follows the block: a new selection, a new
    // correction or a new verdict changes what the panel tints.
    connect(this, &BlockInspector::selectedBoxChanged, this, &BlockInspector::blockDiffChanged);
    // A new selection moves the paint of the selected block.
    connect(this, &BlockInspector::selectedBoxChanged, this, &BlockInspector::blockTextRangeChanged);
}

const BoundingBox *BlockInspector::selectedBox() const
{
    const DocumentModel &document = m_deps.document;
    const int page = m_deps.currentPage();
    if (m_selectedBox < 0 || !document.isValidIndex(page))
        return nullptr;
    const DocumentPage &pageData = document.page(page);
    if (!pageData.recognized || pageData.result.pages.isEmpty())
        return nullptr;
    const QList<BoundingBox> &boxes = pageData.result.pages[0].boxes;
    if (m_selectedBox >= boxes.size())
        return nullptr;
    return &boxes.at(m_selectedBox);
}

QString BlockInspector::selectedBlockText() const
{
    const BoundingBox *box = selectedBox();
    return box ? box->text : QString();
}

QString BlockInspector::selectedBlockLabel() const
{
    const BoundingBox *box = selectedBox();
    return box ? box->label : QString();
}

int BlockInspector::selectedBlockCheckStatus() const
{
    const BoundingBox *box = selectedBox();
    return box ? static_cast<int>(box->checkStatus) : 0;
}

QString BlockInspector::selectedBlockCorrected() const
{
    const BoundingBox *box = selectedBox();
    return box ? box->correctedText : QString();
}

int BlockInspector::selectedBlockCorrectedCheckStatus() const
{
    const BoundingBox *box = selectedBox();
    return box ? static_cast<int>(box->correctedCheckStatus) : 0;
}

double BlockInspector::selectedBlockConfidence() const
{
    const BoundingBox *box = selectedBox();
    return box ? box->confidence : -1.0;
}

double BlockInspector::selectedBlockCorrectedConfidence() const
{
    const BoundingBox *box = selectedBox();
    return box ? box->correctedConfidence : -1.0;
}

int BlockInspector::selectedBlockFixStatus() const
{
    const BoundingBox *box = selectedBox();
    return box ? static_cast<int>(box->fixStatus) : 0;
}

int BlockInspector::selectedBlockSyntaxProblem() const
{
    const BoundingBox *box = selectedBox();
    return box ? static_cast<int>(box->syntaxProblem) : 0;
}

bool BlockInspector::selectedBlockUsesCorrected() const
{
    const BoundingBox *box = selectedBox();
    return box ? box->useCorrectedText : false;
}

QVariantList BlockInspector::selectedBlockTextRange() const
{
    const QList<int> range = blockTextRange(m_selectedBox);
    if (range.isEmpty())
        return {};
    return {range.at(0), range.at(1)};
}

void BlockInspector::setSelectedBoxIndex(int index)
{
    const DocumentModel &document = m_deps.document;
    const int page = m_deps.currentPage();
    int clamped = index;
    if (!document.isValidIndex(page) || !document.page(page).recognized) {
        clamped = -1;
    } else if (index != -1) {
        const int size = document.page(page).result.pages.isEmpty() ? 0 : document.page(page).result.pages[0].boxes.size();
        if (index < 0 || index >= size)
            clamped = -1;
    }
    if (m_selectedBox == clamped)
        return;
    m_selectedBox = clamped;
    emit selectedBoxChanged();
}

const TextDiff &BlockInspector::selectedBlockDiff() const
{
    const BoundingBox *box = selectedBox();
    const QString before = box ? box->text : QString();
    const QString after = box ? box->correctedText : QString();
    if (before != m_diffBefore || after != m_diffAfter) {
        m_diffBefore = before;
        m_diffAfter = after;
        m_diffCache = diffText(before, after);
    }
    return m_diffCache;
}

QVariantList BlockInspector::selectedBlockRemovedRanges() const
{
    const BoundingBox *box = selectedBox();
    // Without a correction there is nothing to compare: the diff of a block
    // and an empty text would tint the whole block.
    if (!box || box->correctedText.isEmpty())
        return {};
    const TextDiff &diff = selectedBlockDiff();
    QVariantList ranges;
    ranges.reserve(diff.removed.size());
    for (const DiffRange &range : diff.removed)
        ranges.append(QVariantMap{{QStringLiteral("start"), range.start}, {QStringLiteral("length"), range.length}});
    return ranges;
}

QVariantList BlockInspector::selectedBlockAddedRanges() const
{
    const BoundingBox *box = selectedBox();
    if (!box || box->correctedText.isEmpty())
        return {};
    const TextDiff &diff = selectedBlockDiff();
    QVariantList ranges;
    ranges.reserve(diff.added.size());
    for (const DiffRange &range : diff.added)
        ranges.append(QVariantMap{{QStringLiteral("start"), range.start}, {QStringLiteral("length"), range.length}});
    return ranges;
}

void BlockInspector::setHighlightDifferences(bool enabled)
{
    if (m_highlightDifferences == enabled)
        return;
    m_highlightDifferences = enabled;
    emit highlightDifferencesChanged();
    emit blockDiffChanged();
}

void BlockInspector::setPageTextEditorFocused(bool focused)
{
    if (m_pageTextEditorFocused == focused)
        return;
    m_pageTextEditorFocused = focused;
    emit pageTextEditorFocusedChanged();
}

bool BlockInspector::blockTextMapped() const
{
    return !currentPageRebuild().ranges.isEmpty();
}

QList<int> BlockInspector::blockTextRange(int boxIndex) const
{
    const auto rebuild = currentPageRebuild();
    for (const BlockTextRange &range : rebuild.ranges) {
        if (range.boxIndex == boxIndex)
            return {range.start, range.length};
    }
    return {};
}

int BlockInspector::boxIndexForTextPosition(int position) const
{
    const auto rebuild = currentPageRebuild();
    if (rebuild.ranges.isEmpty())
        return -1;
    for (const BlockTextRange &range : rebuild.ranges) {
        if (position >= range.start && position < range.start + range.length)
            return range.boxIndex;
    }
    if (position == rebuild.text.size())
        return rebuild.ranges.last().boxIndex;
    return -1;
}

void BlockInspector::attachBlockTextHighlighter(QObject *textDocument, const QColor &color)
{
    QTextDocument *document = nullptr;
    if (auto *wrapper = qobject_cast<QQuickTextDocument *>(textDocument))
        document = wrapper->textDocument();
    else if (auto *plain = qobject_cast<QTextDocument *>(textDocument))
        document = plain;
    if (!document)
        return;
    if (!m_blockHighlighter) {
        m_blockHighlighter = new BlockTextHighlighter(this);
        connect(this, &BlockInspector::blockTextRangeChanged, this, &BlockInspector::updateBlockHighlight);
    }
    m_blockHighlighter->setColor(color);
    m_blockHighlighter->setDocument(document);
    updateBlockHighlight();
}

void BlockInspector::invalidateRebuildCache()
{
    if (m_rebuildCachePage == -1)
        return;  // nothing memoized — the mapping cannot have changed
    m_rebuildCachePage = -1;
    m_rebuildCacheDocRevision = -1;
    m_rebuildCacheCropRevision = -1;
    m_rebuildCacheText.clear();
    m_rebuildCache = {};
    // The ranges the QML property and the highlighter see come from the memo;
    // without this announcement they would wait for the next edit.
    emit blockTextRangeChanged();
}

IOutputParser::RebuiltPageText BlockInspector::currentPageRebuild() const
{
    const DocumentModel &document = m_deps.document;
    const int page = m_deps.currentPage();
    if (!document.isValidIndex(page))
        return {};
    const DocumentPage &pageData = document.page(page);
    if (pageData.result.pages.isEmpty())
        return {};
    const QString shown = m_deps.pageText(page);
    if (m_rebuildCachePage == page && m_rebuildCacheDocRevision == m_deps.docRevision() && m_rebuildCacheCropRevision == m_deps.cropRevision() && m_rebuildCacheText == shown)
        return m_rebuildCache;

    const auto parser = m_deps.parserProvider();
    if (!parser)
        return {};
    IOutputParser::RebuiltPageText rebuild = parser->rebuildTextWithRanges(pageData.result.pages.first());

    if (!rebuild.ranges.isEmpty() && rebuild.text != shown) {
        QList<BlockTextRange> reanchored;
        int searchFrom = 0;
        for (const BlockTextRange &range : rebuild.ranges) {
            const QString blockText = rebuild.text.mid(range.start, range.length);
            if (blockText.isEmpty())
                continue;
            const int pos = shown.indexOf(blockText, searchFrom);
            if (pos < 0)
                continue;
            reanchored.append({range.boxIndex, pos, range.length});
            searchFrom = pos + range.length;
        }
        rebuild.text = shown;
        rebuild.ranges = reanchored;
    }

    m_rebuildCachePage = page;
    m_rebuildCacheDocRevision = m_deps.docRevision();
    m_rebuildCacheCropRevision = m_deps.cropRevision();
    m_rebuildCacheText = shown;
    m_rebuildCache = rebuild;
    return rebuild;
}

void BlockInspector::updateBlockHighlight()
{
    if (!m_blockHighlighter)
        return;
    if (m_selectedBox < 0) {
        // No selected block — the highlight is off; do not pay a page rebuild
        // for it on every keystroke.
        m_blockHighlighter->setStart(-1);
        m_blockHighlighter->setLength(0);
        return;
    }
    const QList<int> range = blockTextRange(m_selectedBox);
    m_blockHighlighter->setStart(range.isEmpty() ? -1 : range.at(0));
    m_blockHighlighter->setLength(range.isEmpty() ? 0 : range.at(1));
}

}  // namespace llocr
