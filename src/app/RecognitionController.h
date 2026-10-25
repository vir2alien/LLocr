#pragma once

#include <functional>
#include <memory>

#include <QFutureWatcher>
#include <QImage>

#include "app/ModelRequestController.h"
#include "core/ConnectionRole.h"
#include "core/OcrRequest.h"
#include "core/StatusMessage.h"
#include "models/OcrModel.h"
#include "runtime/ResolvedConnection.h"

namespace llocr {

class SettingsStore;
class RequestProfileStore;

enum class PageSkip {
    None,
    Unreadable,  ///< a blank replacement for a page the source could not decode
    AlreadyRecognized,
};

class RecognitionController : public ModelRequestController
{
    Q_OBJECT

public:
    using ImageProvider = std::function<QImage(int pageIndex, QString &error)>;
    using SkipResolver = std::function<PageSkip(int pageIndex, bool batch)>;

    explicit RecognitionController(SettingsStore &settings,
                                   RuntimeController &runtime,
                                   RequestProfileStore &requestProfiles,
                                   RequestProfileStore &rawPageOcrProfiles,
                                   ImageProvider imageProvider,
                                   QObject *parent = nullptr,
                                   SkipResolver skipPage = {});

    // task picks the page-recognition pipeline: PageOcr runs the markup-aware
    // page task, RawPageOcr the markup-free one; each brings its own request
    // profile, model and server role.
    void startCurrent(int index, int totalPages, ConnectionRole task = ConnectionRole::PageOcr);
    void startAll(int totalPages, ConnectionRole task = ConnectionRole::PageOcr);
    // The toolbar's scoped spelling: an explicit page set (the thumbnail
    // selection). Explicitly picked pages are recognized even when already
    // recognized; unreadable ones still report as skipped.
    void startPages(const QList<int> &pages, ConnectionRole task = ConnectionRole::PageOcr);

signals:
    // The task tells the caller which pipeline produced the result (the page
    // mode derives from it: PageOcr → MarkupOcr, RawPageOcr → RawOcr).
    void rawResultReady(int pageIndex, llocr::ConnectionRole task, const llocr::OcrResult &raw);

private slots:
    void onRecognitionFinished();

private:
    void ensureConnectionReady();
    void resolveModel();
    int firstPageToRecognize(int from);
    void recognizePage(int index);
    void recognizeNextScopePage();
    bool launchPageRequest(int index);
    QString promptText() const;
    StatusMessage skipReport() const;
    OcrRequest buildRequest(const QImage &image, const ResolvedConnection &conn) const;

    void abortRequest() override;
    void resetRun() override;

    SettingsStore &m_settings;
    RequestProfileStore &m_requestProfiles;
    RequestProfileStore &m_rawPageOcrProfiles;
    ImageProvider m_imageProvider;
    SkipResolver m_skipPage;
    int m_skippedUnreadable = 0;
    int m_skippedRecognized = 0;
    ConnectionRole m_task = ConnectionRole::PageOcr;

    ResolvedConnection m_connection;

    std::unique_ptr<OcrModel> m_model;
    QString m_modelId;
    QString m_modelRole;
    QFutureWatcher<OcrResult> m_watcher;

    int m_totalPages = 0;
    int m_startIndex = 0;
    QList<int> m_pageQueue;  ///< the startPages run; empty for the range modes
    int m_queuePos = 0;
    bool m_recognizeAll = false;
    int m_recognizingIndex = -1;
};

}  // namespace llocr