#include "app/BlockWorkList.h"

#include "app/VerificationPromptStore.h"
#include "config/RequestProfileStore.h"
#include "core/BoxTransitions.h"
#include "parsers/BlockStyle.h"

namespace llocr {

BlockWorkList::BlockWorkList(Deps deps) : m_deps(std::move(deps)) {}

QList<BlockWorkList::Task> BlockWorkList::unrecognizedBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &ocrPage = docPage.result.pages.first();
        const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
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

QList<BlockWorkList::Task> BlockWorkList::readableBlocks(const QList<int> &pages) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        const DocumentPage &docPage = m_deps.document.page(page);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        for (const int box : PageStatus::readableBoxes(docPage.result.pages.first()))
            tasks.append({page, box});
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

bool BlockWorkList::pageRecheckSupported(int pageIndex) const
{
    return !problemBlocks({pageIndex}).isEmpty();
}

bool BlockWorkList::allPageRecheckSupported() const
{
    for (int p = 0; p < m_deps.document.pageCount(); ++p) {
        if (!problemBlocks({p}).isEmpty())
            return true;
    }
    return false;
}

// Blocks created by the layout pass have no text yet; this tells them apart
// from the blocks there is nothing to transcribe for.
bool BlockWorkList::pageBlocksRecognitionSupported(int pageIndex) const
{
    return !unrecognizedBlocks({pageIndex}).isEmpty();
}

}  // namespace llocr
