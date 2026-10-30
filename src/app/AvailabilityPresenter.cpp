#include "app/AvailabilityPresenter.h"
#include "app/VerificationQueueController.h"

namespace llocr {

AvailabilityPresenter::AvailabilityPresenter(Deps deps, QObject *parent) : QObject(parent), m_deps(deps) {}

void AvailabilityPresenter::refresh(const Revisions &revisions)
{
    const int page = m_deps.currentPage();
    const bool importing = m_deps.importing();
    const bool layoutBusy = m_deps.layoutBusy();
    const bool checkBusy = m_deps.checkBusy();
    const bool autoPipelineBusy = m_deps.autoPipelineBusy && m_deps.autoPipelineBusy();
    const bool blockRecognitionModel = blockRecognitionModelAvailable();
    const bool decisionModel = decisionModelAvailable();
    const bool layoutModel = layoutModelAvailable();
    const bool fixModel = fixModelAvailable();

    // The inputs are cheap, the flags below are not: the guard comes first,
    // so a queue that chatters does not rescan the document every time
    // (ADR 208).
    const Fingerprint fingerprint{revisions.docRevision,
                                  revisions.cropRevision,
                                  page,
                                  revisions.pageSelectionRevision,
                                  revisions.verificationFilterRevision,
                                  importing,
                                  layoutBusy,
                                  checkBusy,
                                  autoPipelineBusy,
                                  blockRecognitionModel,
                                  decisionModel,
                                  layoutModel,
                                  fixModel};
    if (fingerprint == m_fingerprint && m_hasFlags)
        return;
    m_fingerprint = fingerprint;

    const Flags current{
        pageVerificationSupported(),
        allPageVerificationSupported(),
        pageProblemRecognitionSupported(),
        allProblemRecognitionSupported(),
        pageBlocksRecognitionSupported(),
        actionsIdle(),
        blockRecognitionModel,
        decisionModel,
        layoutModel,
        fixModel,
        scopeVerificationSupported(),
        scopeBlocksRecognitionSupported(),
        currentPageRecognitionMode(),
        currentPageVerification(),
        selectedPageCount(),
        scopedPendingFixCount(),
        scopeHasResults(),
    };
    if (current == m_flags)
        return;
    m_flags = current;
    m_hasFlags = true;
    emit changed();
}

bool AvailabilityPresenter::pageVerificationSupported() const
{
    return m_deps.verify.pageVerificationSupported(m_deps.currentPage());
}

bool AvailabilityPresenter::allPageVerificationSupported() const
{
    return m_deps.verify.allPageVerificationSupported();
}

bool AvailabilityPresenter::pageProblemRecognitionSupported() const
{
    return m_deps.verify.pageRecheckSupported(m_deps.currentPage());
}

bool AvailabilityPresenter::allProblemRecognitionSupported() const
{
    return m_deps.verify.allPageRecheckSupported();
}

bool AvailabilityPresenter::pageBlocksRecognitionSupported() const
{
    return m_deps.verify.pageBlocksRecognitionSupported(m_deps.currentPage());
}

bool AvailabilityPresenter::actionsIdle() const
{
    if (m_deps.autoPipelineBusy && m_deps.autoPipelineBusy())
        return false;
    return !m_deps.layoutBusy() && !m_deps.checkBusy() && !m_deps.importing();
}

bool AvailabilityPresenter::blockRecognitionModelAvailable() const
{
    return m_deps.runtime.taskModelAvailable(QStringLiteral("blockRecognition"));
}

bool AvailabilityPresenter::decisionModelAvailable() const
{
    return m_deps.runtime.taskModelAvailable(QStringLiteral("decision"));
}

bool AvailabilityPresenter::layoutModelAvailable() const
{
    return m_deps.runtime.taskModelAvailable(QStringLiteral("layout"));
}

bool AvailabilityPresenter::fixModelAvailable() const
{
    return m_deps.runtime.taskModelAvailable(QStringLiteral("fix"));
}

int AvailabilityPresenter::currentPageRecognitionMode() const
{
    const int page = m_deps.currentPage();
    return m_deps.document.isValidIndex(page) ? int(m_deps.document.page(page).recognitionMode) : int(PageRecognitionMode::None);
}

int AvailabilityPresenter::currentPageVerification() const
{
    const int page = m_deps.currentPage();
    if (!m_deps.document.isValidIndex(page) || m_deps.document.page(page).result.pages.isEmpty())
        return int(PageVerification::Incomplete);
    return int(PageStatus::verificationAggregate(m_deps.document.page(page).result.pages.first(), [this](const QString &label) { return m_deps.verification.isTypeEnabled(label); }));
}

int AvailabilityPresenter::selectedPageCount() const
{
    return m_deps.pageModel.selectedCount();
}

int AvailabilityPresenter::scopedPendingFixCount() const
{
    int total = 0;
    for (const int pageIndex : scopePages()) {
        if (!m_deps.document.isValidIndex(pageIndex))
            continue;
        const DocumentPage &page = m_deps.document.page(pageIndex);
        if (!page.recognized || page.result.pages.isEmpty())
            continue;
        total += PageStatus::pendingFixCount(page.result.pages.first());
    }
    return total;
}

bool AvailabilityPresenter::scopeHasResults() const
{
    for (const int pageIndex : scopePages()) {
        if (m_deps.document.isValidIndex(pageIndex) && m_deps.document.page(pageIndex).recognized)
            return true;
    }
    return false;
}

bool AvailabilityPresenter::scopeVerificationSupported() const
{
    for (const int pageIndex : scopePages()) {
        if (m_deps.verify.pageVerificationSupported(pageIndex))
            return true;
    }
    return false;
}

bool AvailabilityPresenter::scopeBlocksRecognitionSupported() const
{
    for (const int pageIndex : scopePages()) {
        if (m_deps.verify.pageBlocksRecognitionSupported(pageIndex))
            return true;
    }
    return false;
}

QList<int> AvailabilityPresenter::scopePages() const
{
    QList<int> pages;
    const bool all = m_deps.pageModel.selectedCount() == 0;
    for (int i = 0; i < m_deps.document.pageCount(); ++i) {
        if (!all && !m_deps.pageModel.data(m_deps.pageModel.index(i), PageListModel::SelectedRole).toBool())
            continue;
        pages.append(i);
    }
    return pages;
}

}  // namespace llocr
