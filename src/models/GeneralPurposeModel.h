#pragma once

#include <QByteArray>
#include <QFuture>
#include <QImage>
#include <QList>
#include <QString>

#include <memory>

#include "core/CheckRequest.h"
#include "core/CheckResult.h"
#include "core/ConnectionConfig.h"
#include "core/LlamaClient.h"

namespace llocr {

// Base class for general-purpose (non-OCR) models used to verify/correct text
// recognized by an OCR model. Analogous to OcrModel: it owns the request
// formation (image + recognized text + prompt + parameters), the async HTTP
// round-trip via LlamaClient, and the response parsing. Derived classes only
// supply model metadata.
class GeneralPurposeModel {
    Q_DISABLE_COPY_MOVE(GeneralPurposeModel)

public:
    GeneralPurposeModel() = default;
    virtual ~GeneralPurposeModel() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    QFuture<CheckResult> check(const CheckRequest &request,
                               const ConnectionConfig &config);
    // Aborts the request of the last check() call. Single-flight contract:
    // only one check() may be in flight at a time (enforced by
    // CheckController::busy), and completion handlers run on the caller
    // thread, so no cross-thread synchronization is needed. There is no
    // check-cancel UI yet; abort() is reserved for it.
    void abort();

protected:
    static QByteArray buildRequestBody(const CheckRequest &request,
                                       const QString &imageDataUrl);
    static CheckResult parseResponse(const QByteArray &responseData);

private:
    static QString encodeImageDataUrl(const QImage &image, const QString &format, int quality = -1);

private:
    // The client of the last check() call — kept alive so abort() can reach
    // it; it carries one QNetworkAccessManager, released on the next check().
    std::shared_ptr<LlamaClient> m_activeClient;
};

} // namespace llocr
