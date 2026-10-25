#include "app/ProjectController.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

#include "core/BoxTransitions.h"

namespace llocr {

ProjectController::ProjectController(Deps deps, QObject *parent) : QObject(parent), m_deps(deps) {}

QString ProjectController::fileName() const
{
    return m_projectPath.isEmpty() ? QString() : QFileInfo(m_projectPath.toLocalFile()).fileName();
}

bool ProjectController::guarded() const
{
    return m_projectBusy || m_deps.busy();
}

bool ProjectController::collect(ProjectData *data, QString *error)
{
    QWriteLocker locker(&m_deps.lock);
    data->currentPage = m_deps.currentPage();

    QHash<QString, int> sourceIds;
    for (int i = 0; i < m_deps.document.pageCount(); ++i) {
        const DocumentPage &page = m_deps.document.page(i);
        const QString key = QDir::fromNativeSeparators(QFileInfo(page.sourcePath).absoluteFilePath());
        int id = sourceIds.value(key, -1);
        if (id < 0) {
            ProjectSource source;
            source.id = data->sources.size();
            source.originalName = QFileInfo(key).fileName();
            source.typeName = ProjectStore::sourceTypeKey(page.sourceType);
            if (QFileInfo::exists(key)) {
                source.sourcePath = key;
            } else if (!page.image.isNull()) {
                // The source file is gone (a temp-dir import, a removed file);
                // embed the rendered copy the page still holds.
                source.fallbackImage = page.image;
            } else {
                if (error)
                    *error = QCoreApplication::translate("ProjectController", "The source file %1 is gone, and the page has no rendered copy to embed.").arg(source.originalName);
                return false;
            }
            id = source.id;
            sourceIds.insert(key, id);
            data->sources.append(source);
        }

        ProjectPageData entry;
        entry.sourceId = id;
        entry.sourcePageIndex = page.sourcePageIndex;
        entry.recognized = page.recognized;
        entry.recognitionMode = page.recognitionMode;
        entry.hasDuplicates = !page.result.pages.isEmpty() && page.result.pages.first().hasDuplicates;
        entry.rotation = page.rotation;
        entry.userOriented = page.userOriented;
        entry.text = page.result.text;
        entry.parseNote = page.parseNote;
        entry.boxes = page.result.pages.isEmpty() ? QList<BoundingBox>() : page.result.pages.first().boxes;
        if (page.recognized) {
            entry.baseline = m_deps.editStore.baseline(i);
            entry.edited = m_deps.editStore.isEdited(i);
        }
        data->pages.append(entry);
    }
    return true;
}

void ProjectController::apply(const ProjectData &data)
{
    {
        QWriteLocker locker(&m_deps.lock);
        m_deps.document.clear();
    }
    m_deps.editStore.clear();
    m_deps.pageModel.clear();
    m_deps.boxModel.setBoxes({});
    m_deps.selectionReset();
    m_deps.setCurrentPage(0);

    struct Occurrence {
        int first = 0;
        int count = 0;
        QSet<int> used;
    };
    QHash<int, QList<Occurrence>> occurrences;
    QList<int> mapped(data.pages.size(), -1);

    QHash<int, int> sourceIndexById;
    for (int i = 0; i < data.sources.size(); ++i)
        sourceIndexById.insert(data.sources.at(i).id, i);

    QStringList skipped;
    for (int p = 0; p < data.pages.size(); ++p) {
        const ProjectPageData &entry = data.pages.at(p);
        const int sourceIndex = sourceIndexById.value(entry.sourceId, -1);
        if (sourceIndex < 0) {
            skipped.append(QString::number(p + 1));
            continue;
        }
        const ProjectSource &source = data.sources.at(sourceIndex);
        if (!source.available) {
            skipped.append(QString::number(p + 1));
            continue;
        }

        int index = -1;
        for (Occurrence &range : occurrences[sourceIndex]) {
            const int slot = entry.sourcePageIndex >= 0 ? entry.sourcePageIndex : 0;
            if (slot < range.count && !range.used.contains(slot)) {
                range.used.insert(slot);
                index = range.first + slot;
                break;
            }
        }
        if (index < 0) {
            const int before = m_deps.document.pageCount();
            QString appendError;
            {
                QWriteLocker locker(&m_deps.lock);
                m_deps.document.appendFile(source.extractedPath, &appendError);
            }
            const int added = m_deps.document.pageCount() - before;
            if (added <= 0) {
                skipped.append(QString::number(p + 1));
                m_deps.problem(StatusMessage::literal(appendError), ProblemLog::Warning);
                continue;
            }
            Occurrence range;
            range.first = before;
            range.count = added;
            const int slot = entry.sourcePageIndex >= 0 ? entry.sourcePageIndex : 0;
            if (slot < added) {
                range.used.insert(slot);
                index = before + slot;
            }
            occurrences[sourceIndex].append(range);
        }
        mapped[p] = index;
    }

    QList<int> kept;
    QList<int> savedIndexForSlot;
    for (int p = 0; p < mapped.size(); ++p) {
        if (mapped.at(p) >= 0) {
            kept.append(mapped.at(p));
            savedIndexForSlot.append(p);
        }
    }
    const QSet<int> keepSet(kept.cbegin(), kept.cend());
    {
        QWriteLocker locker(&m_deps.lock);
        for (int i = m_deps.document.pageCount() - 1; i >= 0; --i) {
            if (!keepSet.contains(i))
                m_deps.document.removePage(i);
        }
    }
    QList<int> sortedKept = kept;
    std::sort(sortedKept.begin(), sortedKept.end());
    QHash<int, int> reindex;
    for (int i = 0; i < sortedKept.size(); ++i)
        reindex.insert(sortedKept.at(i), i);
    for (int &k : kept)
        k = reindex.value(k, -1);
    {
        QWriteLocker locker(&m_deps.lock);
        for (int slot = 0; slot < kept.size(); ++slot) {
            const int from = kept.at(slot);
            if (from == slot)
                continue;
            m_deps.document.movePage(from, slot);
            for (int q = 0; q < kept.size(); ++q) {
                if (q != slot && kept.at(q) >= slot && kept.at(q) < from)
                    ++kept[q];
            }
            kept[slot] = slot;
        }
    }

    m_deps.pageModel.appendPages(kept.size());
    {
        QWriteLocker locker(&m_deps.lock);
        for (int slot = 0; slot < kept.size(); ++slot) {
            const ProjectPageData &entry = data.pages.at(savedIndexForSlot.at(slot));
            DocumentPage &page = m_deps.document.page(slot);
            page.recognized = entry.recognized;
            page.recognitionMode = entry.recognitionMode;
            page.parseNote = entry.parseNote;
            // The turn of the page: the sources are re-rendered from the
            // embedded files, so the turn has to be restored with them.
            page.userOriented = entry.userOriented;
            if (entry.rotation != page.rotation)
                m_deps.document.setRotation(slot, entry.rotation);
            OcrResult result;
            result.success = true;
            OcrPage structured;
            structured.text = entry.text;
            structured.boxes = entry.boxes;
            structured.hasDuplicates = entry.hasDuplicates;
            result.text = entry.text;
            result.pages.append(structured);
            page.result = result;
        }
        // The syntax verdict of a formula is derived from its text: a project
        // written before the rules changed carries the old one (ADR 194).
        for (int slot = 0; slot < m_deps.document.pageCount(); ++slot) {
            DocumentPage &page = m_deps.document.page(slot);
            if (page.result.pages.isEmpty())
                continue;
            for (BoundingBox &box : page.result.pages.first().boxes)
                BoxTransitions::syntaxVerdictRefreshed(box);
        }
    }

    for (int slot = 0; slot < kept.size(); ++slot) {
        const ProjectPageData &entry = data.pages.at(savedIndexForSlot.at(slot));
        m_deps.editStore.reset(slot, entry.baseline);
        if (entry.edited)
            m_deps.editStore.setEdited(slot, true);
        m_deps.pageModel.setRecognized(slot, entry.recognized);
        m_deps.pageModel.setEdited(slot, entry.edited);
        m_deps.pageModel.setHasDuplicates(slot, entry.hasDuplicates);
        m_deps.pageModel.setRecognitionMode(slot, int(entry.recognitionMode));
    }
    m_deps.verificationsRefreshed();

    m_deps.pageModel.setCurrent(0);
    // The upper bound comes from the document, not from the manifest: a project
    // whose embedded files are all missing leaves the document empty, and a
    // clamp to -1 is undefined behaviour (ADR 199).
    const int lastPage = std::max(0, m_deps.document.pageCount() - 1);
    m_deps.setCurrentPage(data.pages.isEmpty() ? 0 : std::clamp(data.currentPage, 0, lastPage));
    m_deps.selectionEnsured();
    // Project labels come from whatever model created the file; the default
    // vocabulary is the only safe overlay mapping here.
    m_deps.boxModel.setStyleModelId(QString());
    m_deps.boxesRefreshed();

    if (!skipped.isEmpty())
        m_deps.problem(StatusMessage::translate("ProjectController", "Project pages %1 could not be restored — see the problem log.").arg(skipped.join(QStringLiteral(", "))), ProblemLog::Warning);
    if (data.pages.isEmpty() || m_deps.document.isEmpty())
        m_deps.status(StatusMessage::translate("ProjectController", "The project contains no pages."));
    else if (skipped.isEmpty())
        m_deps.status(StatusMessage::translate("ProjectController", "Project opened: %1 page(s).").arg(m_deps.document.pageCount()));
    else
        m_deps.status(StatusMessage::translate("ProjectController", "Project opened: %1 page(s), %2 skipped.").arg(m_deps.document.pageCount()).arg(skipped.size()));

    m_deps.documentChanged();
}

void ProjectController::open(const QUrl &fileUrl)
{
    if (guarded())
        return;
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        m_deps.status(StatusMessage::translate("ProjectController", "No project selected."));
        return;
    }

    auto sessionDir = std::make_shared<QTemporaryDir>();
    if (!sessionDir->isValid()) {
        m_deps.status(StatusMessage::translate("ProjectController", "Cannot create a temporary directory for the project."));
        return;
    }

    m_projectBusy = true;
    emit busyChanged();
    m_deps.status(StatusMessage::translate("ProjectController", "Opening project…"));

    auto *watcher = new QFutureWatcher<ProjectStore::LoadResult>(this);
    connect(watcher, &QFutureWatcher<ProjectStore::LoadResult>::finished, this, [this, watcher, path, sessionDir]() {
        watcher->deleteLater();
        ProjectStore::LoadResult result = watcher->result();
        m_projectBusy = false;
        emit busyChanged();

        for (const QString &warning : std::as_const(result.warnings))
            m_deps.problem(StatusMessage::literal(warning), ProblemLog::Warning);
        if (!result.error.isEmpty()) {
            m_deps.status(StatusMessage::translate("ProjectController", "Cannot open the project: %1").arg(result.error));
            m_deps.problem(StatusMessage::literal(result.error), ProblemLog::Error);
            return;
        }

        apply(result.data);
        m_projectSessionDir = sessionDir;
        m_projectPath = QUrl::fromLocalFile(path);
        emit pathChanged();
    });
    const QString extractDir = sessionDir->path();
    watcher->setFuture(QtConcurrent::run([path, extractDir]() { return ProjectStore::load(path, extractDir); }));
}

void ProjectController::save(const QUrl &fileUrl)
{
    if (guarded())
        return;
    if (m_deps.document.isEmpty()) {
        m_deps.status(StatusMessage::translate("ProjectController", "Nothing to save — no pages are open."));
        return;
    }
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        m_deps.status(StatusMessage::translate("ProjectController", "No project selected."));
        return;
    }

    ProjectData data;
    QString error;
    if (!collect(&data, &error)) {
        m_deps.status(StatusMessage::translate("ProjectController", "Cannot save the project: %1").arg(error));
        m_deps.problem(StatusMessage::literal(error), ProblemLog::Error);
        return;
    }

    m_projectBusy = true;
    emit busyChanged();
    m_deps.status(StatusMessage::translate("ProjectController", "Saving project…"));

    auto *watcher = new QFutureWatcher<std::pair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this, [this, watcher, path]() {
        watcher->deleteLater();
        const auto outcome = watcher->result();
        m_projectBusy = false;
        emit busyChanged();
        if (!outcome.first) {
            m_deps.status(StatusMessage::translate("ProjectController", "Cannot save the project: %1").arg(outcome.second));
            m_deps.problem(StatusMessage::literal(outcome.second), ProblemLog::Error);
            return;
        }
        m_projectPath = QUrl::fromLocalFile(path);
        emit pathChanged();
        m_deps.status(StatusMessage::translate("ProjectController", "Project saved: %1").arg(QFileInfo(path).fileName()));
    });
    watcher->setFuture(QtConcurrent::run([path, data]() {
        QString saveError;
        const bool ok = ProjectStore::save(path, data, &saveError);
        return std::make_pair(ok, saveError);
    }));
}

}  // namespace llocr
