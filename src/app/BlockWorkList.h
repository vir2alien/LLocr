#pragma once

#include <functional>
#include <QList>
#include <QString>

#include "app/DocumentModel.h"

namespace llocr {

class RequestProfileStore;
class VerificationPromptStore;

// The work lists over the document's blocks: what each pass would collect
// before a queue runs over it (docs/09, ADR 257 §6 — collection is separated
// from execution). Pure queries; the mutual-exclusion and busy checks stay
// with the callers.
class BlockWorkList
{
    Q_DISABLE_COPY_MOVE(BlockWorkList)

public:
    // One block of the work list: the page and the box on it.
    struct Task {
        int page;
        int box;
    };

    // Which block types a pass handles. Empty — every type. A stage that
    // reads only some types narrows the collection with it (ADR 257).
    using TypeFilter = std::function<bool(const QString &blockType)>;

    struct Deps {
        DocumentModel &document;
        VerificationPromptStore &verification;
        // The active profile of the blockRecognition role names the block
        // styles: an image placeholder is never transcribed.
        RequestProfileStore &checkRequestProfiles;
    };

    explicit BlockWorkList(Deps deps);

    // The blocks with no text yet (the layout-pass ones); image placeholders
    // are skipped.
    QList<Task> unrecognizedBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    // The blocks the decision stage judges: the enabled types and the
    // duplicate suspects, with a text and no verdict on the effective layer.
    QList<Task> enabledBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    // The blocks the fixer still has to answer (ADR 188): the decision model
    // left them open and the fixer has not looked at this candidate yet.
    QList<Task> fixableBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    // The statuses that ask for a re-run: the decision stage rejected the
    // text (Mismatch), the block could not be transcribed (Review) or the
    // formula's syntax was rejected (ADR 194).
    QList<Task> problemBlocks(const QList<int> &pages) const;
    // The blocks the reader takes (ADR 260): every text block — with a text
    // or without one; the reader writes the block from the image.
    QList<Task> readableBlocks(const QList<int> &pages) const;

    // The re-run collectors: the same scans with the already-answered blocks
    // included, for a pass that overwrites (a re-run). A block the user
    // wrote or confirmed by hand never comes back.
    QList<Task> reRecognizableBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    QList<Task> recheckableBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    QList<Task> refixableBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    // The read re-run continues where the reader stopped: the readable blocks
    // minus the ones the reader has answered.
    QList<Task> notReadBlocks(const QList<int> &pages) const;

    // The early-exit forms behind the re-run confirmations: there is finished
    // work on the pages a new pass would rewrite.
    bool hasRecognizedBlocks(const QList<int> &pages) const;
    bool hasVerifiedBlocks(const QList<int> &pages) const;
    bool hasRemedyResults(const QList<int> &pages) const;
    bool hasReadResults(const QList<int> &pages) const;

    // The enabled blocks of one page with the counts the «nothing to check»
    // report is built from: the blocks that already carry a verdict and the
    // formulas rejected on their LaTeX syntax (ADR 194).
    QList<Task> enabledBoxesOnPage(int pageIndex, int *answered = nullptr, int *rejected = nullptr, const TypeFilter &filter = {}) const;

    // The page-level enablement of the toolbar actions; the callers add the
    // mutual-exclusion checks (the markup pass, the running queues).
    bool pageVerificationSupported(int pageIndex) const;
    bool allPageVerificationSupported() const;
    bool pageRecheckSupported(int pageIndex) const;
    bool allPageRecheckSupported() const;
    bool pageBlocksRecognitionSupported(int pageIndex) const;

private:
    // The early-exit forms behind the predicates: a first matching block
    // answers, no work list is built.
    bool hasProblemBlocks(int pageIndex) const;
    bool hasUnrecognizedBlocks(int pageIndex) const;

    Deps m_deps;
};

}  // namespace llocr
