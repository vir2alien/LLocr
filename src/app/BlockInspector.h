#pragma once

#include <functional>
#include <memory>
#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

#include "core/TextDiff.h"
#include "parsers/IOutputParser.h"

namespace llocr {

class BlockTextHighlighter;
class DocumentModel;

// The selected block, as the panel reads it (ADR 213): the two text layers
// with their verdicts, the diff between them and the place of the block in
// the page text. The document belongs to the controller; the inspector owns
// the selection and the memos the panel's reads need.
class BlockInspector : public QObject
{
    Q_OBJECT

public:
    struct Deps {
        DocumentModel &document;
        // The page the user reads: the selection, the diff and the block-text
        // mapping follow it.
        std::function<int()> currentPage;
        std::function<int()> docRevision;
        std::function<int()> cropRevision;
        // The text shown on the page (the user's own edit included).
        std::function<QString(int)> pageText;
        // The parser that rebuilds the page text from the blocks.
        std::function<std::unique_ptr<IOutputParser>()> parserProvider;
    };

    explicit BlockInspector(Deps deps, QObject *parent = nullptr);

    int selectedBoxIndex() const { return m_selectedBox; }
    void setSelectedBoxIndex(int index);
    // Drops the selection without announcing it: a document swap replaces the
    // blocks, so the old index means nothing.
    void resetSelection() { m_selectedBox = -1; }
    const BoundingBox *selectedBox() const;

    QString selectedBlockText() const;
    QString selectedBlockLabel() const;
    int selectedBlockCheckStatus() const;
    QString selectedBlockCorrected() const;
    int selectedBlockCorrectedCheckStatus() const;
    double selectedBlockConfidence() const;
    double selectedBlockCorrectedConfidence() const;
    int selectedBlockFixStatus() const;
    int selectedBlockSyntaxProblem() const;
    bool selectedBlockUsesCorrected() const;
    QVariantList selectedBlockTextRange() const;
    QVariantList selectedBlockRemovedRanges() const;
    QVariantList selectedBlockAddedRanges() const;

    bool highlightDifferences() const { return m_highlightDifferences; }
    void setHighlightDifferences(bool enabled);
    bool pageTextEditorFocused() const { return m_pageTextEditorFocused; }
    void setPageTextEditorFocused(bool focused);

    bool blockTextMapped() const;
    QList<int> blockTextRange(int boxIndex) const;
    int boxIndexForTextPosition(int position) const;
    void attachBlockTextHighlighter(QObject *textDocument, const QColor &color);
    void invalidateRebuildCache();
    // The page text changed under the blocks: the mapping and the paint of the
    // selected block must be recomputed.
    void announceTextRangeChanged() { emit blockTextRangeChanged(); }
    // The selected block changed its state (a verdict, a correction, an edit):
    // the panel re-reads it.
    void blockStateChanged() { emit selectedBoxChanged(); }

signals:
    void selectedBoxChanged();
    void blockTextRangeChanged();
    // The panel's diff view changed: a new block, a new correction or the
    // highlight toggle.
    void blockDiffChanged();
    void highlightDifferencesChanged();
    void pageTextEditorFocusedChanged();

private:
    const TextDiff &selectedBlockDiff() const;
    IOutputParser::RebuiltPageText currentPageRebuild() const;
    void updateBlockHighlight();

    Deps m_deps;
    int m_selectedBox = -1;
    bool m_highlightDifferences = true;
    bool m_pageTextEditorFocused = false;
    BlockTextHighlighter *m_blockHighlighter = nullptr;  ///< owned; created on first attach

    // The diff of the selected block: the removed and the added ranges are two
    // views of one comparison, and the comparison itself is quadratic — so it
    // is computed once for the pair of texts and kept until the texts change
    // (ADR 208).
    mutable TextDiff m_diffCache;
    mutable QString m_diffBefore;
    mutable QString m_diffAfter;

    // Memo of currentPageRebuild() keyed by its inputs (page, the structural
    // revisions, the shown text) plus parser options: a keystroke re-reads the
    // rebuild several times (highlight, QML range property), and the regex
    // pass over the blocks must not run per read.
    mutable IOutputParser::RebuiltPageText m_rebuildCache;
    mutable int m_rebuildCachePage = -1;
    mutable int m_rebuildCacheDocRevision = -1;
    mutable int m_rebuildCacheCropRevision = -1;
    mutable QString m_rebuildCacheText;
};

}  // namespace llocr
