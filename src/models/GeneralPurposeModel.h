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
    static QByteArray buildRequestBody(const CheckRequest &request,
                                       const QString &imageDataUrl);
    static CheckResult parseResponse(const QByteArray &responseData);

private:
    static QString encodeImageDataUrl(const QImage &image, const QString &format, int quality = -1);

private:
    std::shared_ptr<LlamaClient> m_activeClient;
};

} // namespace llocr
