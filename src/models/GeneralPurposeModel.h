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

class GeneralPurposeModel {
    Q_DISABLE_COPY_MOVE(GeneralPurposeModel)

public:
    GeneralPurposeModel() = default;
    virtual ~GeneralPurposeModel() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    QFuture<CheckResult> check(const CheckRequest &request,
                               const ConnectionConfig &config);

    void abort();

protected:
    // Static for the same reason as OcrModel: check() must complete even after
    // the adapter is destroyed (ADR 111).
    static QByteArray buildRequestBody(const CheckRequest &request,
                                       const QByteArray &imageDataUrl);
    static CheckResult parseResponse(const QByteArray &responseData);

private:
    std::shared_ptr<LlamaClient> m_activeClient;
};

} // namespace llocr
