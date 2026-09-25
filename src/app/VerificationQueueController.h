#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <functional>

#include "app/CheckController.h"
#include "app/DocumentModel.h"
#include "app/VerificationPromptStore.h"
#include "core/CheckResult.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class RequestProfileStore;

class VerificationQueueController : public QObject
{
    Q_OBJECT

public:
    struct Deps {
        DocumentModel &document;
        VerificationPromptStore &verification;
        RequestProfileStore &checkRequestProfiles;
        RuntimeController &runtime;
        std::function<QImage(int pageIndex, int boxIndex)> cropProvider;
        std::function<bool()> recognitionBusy;
    };

    explicit VerificationQueueController(Deps deps, QObject *parent = nullptr);

    bool checkBusy() const { return m_check.busy(); }
    bool queueActive() const { return m_verifyQueueActive; }
    bool finished() const { return m_checkFinished; }
    int progressDone() const { return m_verifyDone; }
    int progressTotal() const { return m_verifyTotal; }
    QString errorMessage() const { return m_checkError; }

    bool pageVerificationSupported(int pageIndex) const;
    bool allPageVerificationSupported() const;

public slots:
    void checkBlock(int pageIndex, int boxIndex);
    void checkPageEnabledBlocks(int pageIndex);
    void checkAllEnabledBlocks(bool onlyUnchecked = false);
    void stop();

signals:
    void stateChanged();
    void statusRequested(const QString &message);
    void blockChecked(int pageIndex, int boxIndex, const CheckResult &result);

private:
    struct VerifyTask {
        int page;
        int box;
    };

    void startVerifyQueue(const QList<VerifyTask> &tasks);
    void startNextVerify();
    void finishVerifyQueue();
    void collectEnabledBoxes(int pageIndex, QList<int> &out,
                             bool onlyUnchecked) const;

    Deps m_deps;
    CheckController m_check;

    QList<VerifyTask> m_verifyQueue;
    int m_verifyPage = -1;
    int m_verifyBoxIndex = -1;
    bool m_verifyQueueActive = false;
    bool m_checkFinished = false;
    int m_verifyTotal = 0;
    int m_verifyDone = 0;
    QString m_checkError;
};

}  // namespace llocr
