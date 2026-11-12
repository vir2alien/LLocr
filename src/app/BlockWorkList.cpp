#include "app/BlockWorkList.h"

#include "app/VerificationPromptStore.h"
#include "config/RequestProfileStore.h"
#include "core/BoxTransitions.h"
#include "parsers/BlockStyle.h"

namespace llocr {

namespace {

// A block the user wrote or confirmed by hand — on either layer, a re-run
// never rewrites or re-judges it.
bool userOwned(const BoundingBox &box)
{
    return BoxTransitions::userOwned(box);
}
// The reader's three outcomes (ADR 260).
bool readAnswered(BoxFixStatus status)
{
    return status == BoxFixStatus::Read || status == BoxFixStatus::ReadFixed || status == BoxFixStatus::ReadDamaged;
}

// The decision model's verdict on the block: on either layer. A stored
// correction does not take the text layer's verdict away — the check has
// answered the block either way (ADR 311).
bool verdictGiven(const BoundingBox &box)
{
    const auto verdict = [](BoxCheckStatus status) { return status == BoxCheckStatus::Ok || status == BoxCheckStatus::Mismatch || status == BoxCheckStatus::Review; };
    return verdict(box.checkStatus) || verdict(box.correctedCheckStatus);
}

}  // namespace

BlockWorkList::BlockWorkList(Deps deps) : m_deps(std::move(deps)) {}

QList<BlockWorkList::Task> BlockWorkList::unrecognizedBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        for (int i = 0; i < ocrPage.boxes.size(); ++i) {
            const BoundingBox &box = ocrPage.boxes.at(i);
            if (filter && !filter(box.label))
                continue;
            if (box.checkStatus != BoxCheckStatus::NotChecked || !box.text.isEmpty() || !box.correctedText.isEmpty())
                continue;
            if (blockStyleForLabel(box.label, modelId).style == BlockStyle::ImagePlaceholder)
                continue;
            tasks.append({page, i});
        }
    }
    return tasks;
}

QList<BlockWorkList::Task> BlockWorkList::enabledBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    for (const int page : pages)
        tasks.append(enabledBoxesOnPage(page, nullptr, nullptr, filter));
    return tasks;
}

QList<BlockWorkList::Task> BlockWorkList::fixableBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        for (int i = 0; i < ocrPage.boxes.size(); ++i) {
            if (filter && !filter(ocrPage.boxes.at(i).label))
                continue;
            if (BoxTransitions::needsFix(ocrPage.boxes.at(i)))
                tasks.append({page, i});
        }
    }
    return tasks;
}

QList<BlockWorkList::Task> BlockWorkList::problemBlocks(const QList<int> &pages) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        for (int i = 0; i < ocrPage.boxes.size(); ++i) {
            const BoxCheckStatus status = BoxTransitions::effectiveCheckStatus(ocrPage.boxes.at(i));
            // A formula the app rejected on its LaTeX syntax (ADR 194) needs a
            // new recognition more than a re-check does: the text itself is
            // broken.
            if (status == BoxCheckStatus::Mismatch || status == BoxCheckStatus::Review || status == BoxCheckStatus::InvalidSyntax)
                tasks.append({page, i});
        }
    }
    return tasks;
}

QList<BlockWorkList::Task> BlockWorkList::readBlocks(const QList<int> &pages, bool includeAnswered) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        // A checked page: only the blocks the decision model rejected — the
        // set the fixer takes. An unchecked page: every text block, as before
        // (ADR 311).
        QList<int> candidates;
        if (pageChecked(page)) {
            for (int i = 0; i < ocrPage.boxes.size(); ++i) {
                if (BoxTransitions::needsFix(ocrPage.boxes.at(i)))
                    candidates.append(i);
            }
        } else {
            candidates = PageStatus::readableBoxes(ocrPage);
        }
        for (const int i : candidates) {
            const BoundingBox &box = ocrPage.boxes.at(i);
            if (userOwned(box))
                continue;
            if (includeAnswered || !readAnswered(box.fixStatus))
                tasks.append({page, i});
        }
        if (!includeAnswered)
            continue;
        // The redo covers the blocks the reader has already answered too —
        // the shape of the fix's redo (ADR 311).
        for (int i = 0; i < ocrPage.boxes.size(); ++i) {
            const BoundingBox &box = ocrPage.boxes.at(i);
            if (!readAnswered(box.fixStatus) || userOwned(box))
                continue;
            if (blockStyleForLabel(box.label).style == BlockStyle::ImagePlaceholder)
                continue;
            if (candidates.contains(i))
                continue;
            tasks.append({page, i});
        }
    }
    return tasks;
}

bool BlockWorkList::pageChecked(int pageIndex) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return false;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return false;
    bool asked = false;
    for (const BoundingBox &box : std::as_const(docPage.result.pages.first().boxes)) {
        if (!m_deps.verification.isTypeEnabled(box.label) && !box.duplicateSuspect)
            continue;
        // The block the user owns is not the checker's business, and a
        // formula with broken LaTeX is never asked about (ADR 194).
        if (userOwned(box))
            continue;
        if (BoxTransitions::effectiveCheckStatus(box) == BoxCheckStatus::InvalidSyntax)
            continue;
        if (BoxTransitions::effectiveText(box).isEmpty())
            continue;
        asked = true;
        if (!verdictGiven(box))
            return false;
    }
    return asked;
}

QList<BlockWorkList::Task> BlockWorkList::reRecognizableBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        for (int i = 0; i < ocrPage.boxes.size(); ++i) {
            const BoundingBox &box = ocrPage.boxes.at(i);
            if (filter && !filter(box.label))
                continue;
            if (userOwned(box))
                continue;
            if (blockStyleForLabel(box.label, modelId).style == BlockStyle::ImagePlaceholder)
                continue;
            tasks.append({page, i});
        }
    }
    return tasks;
}

QList<BlockWorkList::Task> BlockWorkList::recheckableBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        for (int i = 0; i < ocrPage.boxes.size(); ++i) {
            const BoundingBox &box = ocrPage.boxes.at(i);
            if (filter && !filter(box.label))
                continue;
            if (!m_deps.verification.isTypeEnabled(box.label) && !box.duplicateSuspect)
                continue;
            if (userOwned(box))
                continue;
            // A formula the app rejected on its LaTeX syntax waits for a new
            // recognition, not for another verdict (ADR 194).
            if (BoxTransitions::effectiveCheckStatus(box) == BoxCheckStatus::InvalidSyntax)
                continue;
            if (BoxTransitions::effectiveText(box).isEmpty())
                continue;
            tasks.append({page, i});
        }
    }
    return tasks;
}

QList<BlockWorkList::Task> BlockWorkList::refixableBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        for (int i = 0; i < ocrPage.boxes.size(); ++i) {
            const BoundingBox &box = ocrPage.boxes.at(i);
            if (filter && !filter(box.label))
                continue;
            if (userOwned(box))
                continue;
            if (!BoxTransitions::needsFix(box) && box.fixStatus == BoxFixStatus::None)
                continue;
            tasks.append({page, i});
        }
    }
    return tasks;
}

QList<BlockWorkList::Task> BlockWorkList::enabledBoxesOnPage(int pageIndex, int *answered, int *rejected, const TypeFilter &filter) const
{
    if (answered)
        *answered = 0;
    if (rejected)
        *rejected = 0;
    QList<Task> tasks;
    if (!m_deps.document.isValidIndex(pageIndex))
        return tasks;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return tasks;
    const OcrPage &ocrPage = docPage.result.pages.first();
    for (int i = 0; i < ocrPage.boxes.size(); ++i) {
        const BoundingBox &box = ocrPage.boxes.at(i);
        if (filter && !filter(box.label))
            continue;
        if (!m_deps.verification.isTypeEnabled(box.label) && !box.duplicateSuspect)
            continue;
        // A formula the app rejected on its LaTeX syntax is not asked about
        // (ADR 194): the answer would be about a text that cannot be
        // rendered. It waits for a new recognition instead.
        if (BoxTransitions::effectiveCheckStatus(box) == BoxCheckStatus::InvalidSyntax) {
            if (rejected)
                ++*rejected;
            continue;
        }
        // A block with a verdict on its effective layer has been answered; a
        // re-recognized block (fresh text, verdicts erased) is asked again.
        if (BoxTransitions::effectiveCheckStatus(box) != BoxCheckStatus::NotChecked) {
            if (answered)
                ++*answered;
            continue;
        }
        if (BoxTransitions::effectiveText(box).isEmpty())
            continue;
        tasks.append({pageIndex, i});
    }
    return tasks;
}

bool BlockWorkList::pageVerificationSupported(int pageIndex) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return false;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return false;
    for (const BoundingBox &box : std::as_const(docPage.result.pages.first().boxes)) {
        if ((m_deps.verification.isTypeEnabled(box.label) || box.duplicateSuspect) && !BoxTransitions::effectiveText(box).isEmpty())
            return true;
    }
    return false;
}

bool BlockWorkList::allPageVerificationSupported() const
{
    for (int p = 0; p < m_deps.document.pageCount(); ++p) {
        const DocumentPage &docPage = m_deps.document.page(p);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        for (const BoundingBox &box : std::as_const(docPage.result.pages.first().boxes)) {
            if ((m_deps.verification.isTypeEnabled(box.label) || box.duplicateSuspect) && !BoxTransitions::effectiveText(box).isEmpty())
                return true;
        }
    }
    return false;
}

// The early-exit forms the availability predicates read: a first problem
// block or a first block without text answers the question, no list is built.
bool BlockWorkList::hasProblemBlocks(int pageIndex) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return false;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return false;
    const OcrPage &ocrPage = docPage.result.pages.first();
    for (int i = 0; i < ocrPage.boxes.size(); ++i) {
        const BoxCheckStatus status = BoxTransitions::effectiveCheckStatus(ocrPage.boxes.at(i));
        if (status == BoxCheckStatus::Mismatch || status == BoxCheckStatus::Review || status == BoxCheckStatus::InvalidSyntax)
            return true;
    }
    return false;
}

bool BlockWorkList::hasUnrecognizedBlocks(int pageIndex) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return false;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return false;
    const OcrPage &ocrPage = docPage.result.pages.first();
    const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
    for (int i = 0; i < ocrPage.boxes.size(); ++i) {
        const BoundingBox &box = ocrPage.boxes.at(i);
        if (box.checkStatus != BoxCheckStatus::NotChecked || !box.text.isEmpty() || !box.correctedText.isEmpty())
            continue;
        if (blockStyleForLabel(box.label, modelId).style == BlockStyle::ImagePlaceholder)
            continue;
        return true;
    }
    return false;
}

bool BlockWorkList::hasRecognizedBlocks(const QList<int> &pages) const
{
    const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        for (int i = 0; i < ocrPage.boxes.size(); ++i) {
            const BoundingBox &box = ocrPage.boxes.at(i);
            if (blockStyleForLabel(box.label, modelId).style == BlockStyle::ImagePlaceholder)
                continue;
            if (box.checkStatus == BoxCheckStatus::NotChecked && box.text.isEmpty() && box.correctedText.isEmpty())
                continue;
            return true;
        }
    }
    return false;
}

bool BlockWorkList::hasVerifiedBlocks(const QList<int> &pages) const
{
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        for (int i = 0; i < ocrPage.boxes.size(); ++i) {
            const BoundingBox &box = ocrPage.boxes.at(i);
            if (!m_deps.verification.isTypeEnabled(box.label) && !box.duplicateSuspect)
                continue;
            // A formula the app rejected on its LaTeX syntax is never asked
            // about (ADR 194): the checker has not run there.
            if (BoxTransitions::effectiveCheckStatus(box) == BoxCheckStatus::InvalidSyntax)
                continue;
            if (BoxTransitions::effectiveCheckStatus(box) == BoxCheckStatus::NotChecked)
                continue;
            if (BoxTransitions::effectiveText(box).isEmpty())
                continue;
            return true;
        }
    }
    return false;
}

bool BlockWorkList::hasRemedyResults(const QList<int> &pages) const
{
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        for (const BoundingBox &box : std::as_const(docPage.result.pages.first().boxes)) {
            if (box.fixStatus != BoxFixStatus::None)
                return true;
        }
    }
    return false;
}

bool BlockWorkList::hasReadResults(const QList<int> &pages) const
{
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        for (const BoundingBox &box : std::as_const(docPage.result.pages.first().boxes)) {
            if (readAnswered(box.fixStatus))
                return true;
        }
    }
    return false;
}

bool BlockWorkList::pageRecheckSupported(int pageIndex) const
{
    return hasProblemBlocks(pageIndex);
}

bool BlockWorkList::allPageRecheckSupported() const
{
    for (int p = 0; p < m_deps.document.pageCount(); ++p) {
        if (hasProblemBlocks(p))
            return true;
    }
    return false;
}

// Blocks created by the layout pass have no text yet; this tells them apart
// from the blocks there is nothing to transcribe for.
bool BlockWorkList::pageBlocksRecognitionSupported(int pageIndex) const
{
    return hasUnrecognizedBlocks(pageIndex);
}

}  // namespace llocr
