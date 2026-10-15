#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include "app/DocumentModel.h"
#include "core/OcrResult.h"

namespace llocr {

struct ProjectSource {
    int id = 0;
    QString originalName;  // basename of the imported file, for the UI/errors
    QString typeName;      // "image" | "pdf" | "djvu"
    QString sourcePath;
    QImage fallbackImage;
    QString extractedPath;
    bool available = true;  // load side: the embedded file exists
};

struct ProjectPageData {
    int sourceId = 0;
    int sourcePageIndex = -1;  // pdf/djvu only
    bool recognized = false;
    bool edited = false;
    bool hasDuplicates = false;
    QString text;      // effective page text, manual edits included
    QString baseline;  // recognized text, the "revert" target
    QString parseNote;
    QList<BoundingBox> boxes;
};

struct ProjectData {
    int currentPage = 0;
    QList<ProjectSource> sources;
    QList<ProjectPageData> pages;
};

class ProjectStore
{
public:
    // 2: boxes carry the correction layer's verdict (ADR 164). v1 projects
    // keep loading: checkStatus 2 (the removed ReRecognized) reads as
    // NotChecked on both layers, a stored correctedText survives as the
    // correction layer.
    static constexpr int kSchemaVersion = 2;
    static constexpr QLatin1StringView kFormatId{"llocr-project"};
    static constexpr QLatin1StringView kProjectEntry{"project.json"};

    struct LoadResult {
        ProjectData data;
        QString error;
        QStringList warnings;
    };

    static QString suffix() { return QStringLiteral(".llocr"); }

    static QString sourceTypeKey(DocumentSource type);

    static bool save(const QString &path, const ProjectData &data, QString *error);

    static LoadResult load(const QString &path, const QString &extractDir);
};

}  // namespace llocr
