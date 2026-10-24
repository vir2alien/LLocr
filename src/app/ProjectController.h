#pragma once

#include <functional>
#include <memory>
#include <QObject>
#include <QReadWriteLock>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include "app/BoxListModel.h"
#include "app/DocumentModel.h"
#include "app/PageEditStore.h"
#include "app/PageListModel.h"
#include "app/ProblemLog.h"
#include "app/ProjectStore.h"
#include "core/StatusMessage.h"

namespace llocr {

// The project: a self-contained .llocr file with the pages, their sources and
// the recognized blocks (ADR 131). The controller owns the file lifecycle —
// reading, writing, the busy flag and the path — and restores the document it
// reads (ADR 213).
class ProjectController : public QObject
{
    Q_OBJECT

public:
    struct Deps {
        DocumentModel &document;
        PageListModel &pageModel;
        BoxListModel &boxModel;
        PageEditStore &editStore;
        QReadWriteLock &lock;
        // The page the user reads: a restored project lands on the saved one.
        std::function<int()> currentPage;
        std::function<void(int)> setCurrentPage;
        // The guard: a project is read or written only when nothing else runs.
        std::function<bool()> busy;
        // The host's side effects: the status line, the problem log and the
        // views a restored document needs.
        std::function<void(const StatusMessage &)> status;
        std::function<void(const StatusMessage &, ProblemLog::Severity)> problem;
        std::function<void()> selectionReset;
        std::function<void()> verificationsRefreshed;
        std::function<void()> selectionEnsured;
        std::function<void()> boxesRefreshed;
        std::function<void()> documentChanged;
    };

    explicit ProjectController(Deps deps, QObject *parent = nullptr);

    bool busy() const { return m_projectBusy; }
    QUrl path() const { return m_projectPath; }
    QString fileName() const;

public slots:
    void open(const QUrl &fileUrl);
    void save(const QUrl &fileUrl);

signals:
    void busyChanged();
    void pathChanged();

private:
    bool collect(ProjectData *data, QString *error);
    void apply(const ProjectData &data);
    bool guarded() const;

    Deps m_deps;
    bool m_projectBusy = false;
    QUrl m_projectPath;
    std::shared_ptr<QTemporaryDir> m_projectSessionDir;
};

}  // namespace llocr
