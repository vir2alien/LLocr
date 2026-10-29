#pragma once

#include <functional>
#include <QList>
#include <QObject>

#include "app/DocumentModel.h"
#include "app/PageListModel.h"
#include "app/VerificationPromptStore.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class VerificationQueueController;

// The availability matrix: which actions the document, the runtime and the
// running jobs allow right now (ADR 213). The presenter owns the flags and
// announces a change only when one of them actually flips, so a queue that
// chatters does not make QML rescan the document.
class AvailabilityPresenter : public QObject
{
    Q_OBJECT

public:
    struct Deps {
        DocumentModel &document;
        PageListModel &pageModel;
        VerificationQueueController &verify;
        RuntimeController &runtime;
        VerificationPromptStore &verification;
        // The live inputs: the page the user reads and what runs right now.
        std::function<int()> currentPage;
        std::function<bool()> importing;
        std::function<bool()> layoutBusy;
        std::function<bool()> checkBusy;
    };

    // The counters the flags are keyed on: the document changed, the blocks
    // changed, the page selection changed or the verification filter changed.
    struct Revisions {
        int docRevision = -1;
        int cropRevision = -1;
        int pageSelectionRevision = -1;
        int verificationFilterRevision = -1;
    };

    explicit AvailabilityPresenter(Deps deps, QObject *parent = nullptr);

    // Recomputes the flags; changed() fires only when one of them flips.
    void refresh(const Revisions &revisions);

    bool pageVerificationSupported() const;
    bool allPageVerificationSupported() const;
    bool pageProblemRecognitionSupported() const;
    bool allProblemRecognitionSupported() const;
    bool pageBlocksRecognitionSupported() const;
    bool actionsIdle() const;
    bool blockRecognitionModelAvailable() const;
    bool decisionModelAvailable() const;
    bool layoutModelAvailable() const;
    bool fixModelAvailable() const;
    int currentPageRecognitionMode() const;
    int currentPageVerification() const;
    int selectedPageCount() const;
    int scopedPendingFixCount() const;
    bool scopeHasResults() const;
    bool scopeVerificationSupported() const;
    bool scopeBlocksRecognitionSupported() const;

    // The toolbar's scope: the thumbnail selection, empty = every page. The
    // scoped actions and the availability of the scope read it here.
    QList<int> scopePages() const;

signals:
    void changed();

private:
    // The values behind the NOTIFY signal; the signal fires only when one of
    // them actually flips, so a per-block queue update does not make QML
    // rescan the document (C++-21).
    struct Flags {
        bool pageVerification = false;
        bool allPageVerification = false;
        bool pageProblemRecognition = false;
        bool allProblemRecognition = false;
        bool pageBlocksRecognition = false;
        bool actionsIdle = false;
        bool blockRecognitionModel = false;
        bool decisionModel = false;
        bool layoutModel = false;
        bool fixModel = false;
        bool scopeVerification = false;
        bool scopeBlocksRecognition = false;
        int currentPageMode = int(PageRecognitionMode::None);
        int currentPageVerification = int(PageVerification::Incomplete);
        int selectedPages = 0;
        int scopedProblemBlocks = 0;
        bool scopeHasResults = false;
        bool operator==(const Flags &) const = default;
    };

    // The cheap fingerprint of the inputs the flags are computed from. It is
    // compared before any of the expensive getters runs: the flags cost a
    // scan of the document, and the queue's chatter must not pay for it
    // (ADR 208).
    struct Fingerprint {
        int docRevision = -1;
        int cropRevision = -1;
        int currentPage = -1;
        int pageSelectionRevision = -1;
        int verificationFilterRevision = -1;
        bool importing = false;
        bool layoutBusy = false;
        bool checkBusy = false;
        bool blockRecognitionModel = false;
        bool decisionModel = false;
        bool layoutModel = false;
        bool fixModel = false;
        bool operator==(const Fingerprint &) const = default;
    };

    Deps m_deps;
    Flags m_flags;
    Fingerprint m_fingerprint;
    bool m_hasFlags = false;
};

}  // namespace llocr
