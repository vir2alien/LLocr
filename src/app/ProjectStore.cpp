#include "app/ProjectStore.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include "app/PageRotation.h"
#include "runtime/ArchiveExtractor.h"
#include "runtime/ZipWriter.h"

namespace llocr {

namespace {

QString sanitizeName(const QString &name)
{
    QString result;
    result.reserve(name.size());
    for (const QChar ch : name) {
        const bool keep =
            ch.isLetterOrNumber() || ch == QLatin1Char('.') || ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char(' ') || ch == QLatin1Char('(') || ch == QLatin1Char(')');
        result.append(keep ? ch : QLatin1Char('_'));
    }
    while (result.startsWith(QLatin1Char('.')))
        result.remove(0, 1);
    return result.trimmed();
}

// The manifest is an untrusted input: its "file" field must name a file inside
// the extraction directory. QDir::filePath() does not normalize ".." and returns
// an absolute argument as is, so the value is validated here instead of being
// pasted into the path.
QString containedPath(const QString &extractDir, const QString &file)
{
    if (file.isEmpty())
        return {};
    if (file.startsWith(QLatin1Char('/')) || file.startsWith(QLatin1Char('\\')))
        return {};
    if (file.size() >= 2 && file.at(1) == QLatin1Char(':'))
        return {};

    QStringList parts;
    QString segment;
    for (const QChar ch : file) {
        if (ch == QLatin1Char('/') || ch == QLatin1Char('\\')) {
            parts.append(segment);
            segment.clear();
            continue;
        }
        segment.append(ch);
    }
    parts.append(segment);
    parts.removeAll(QString());

    if (parts.isEmpty())
        return {};
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String(".."))
            return {};
    }

    const QString root = QDir::cleanPath(extractDir);
    const QString candidate = QDir::cleanPath(QDir(root).filePath(parts.join(QLatin1Char('/'))));
    if (candidate != root && !candidate.startsWith(root + QLatin1Char('/')))
        return {};
    return candidate;
}

QString suffixOf(const QString &name)
{
    const QString suffix = QFileInfo(name).suffix();
    return suffix.isEmpty() ? QStringLiteral("bin") : suffix.toLower();
}

QString assignEntryName(int id, const QString &originalName, QSet<QString> *used)
{
    QString base = QString::number(id) + QLatin1Char('_') + sanitizeName(originalName);
    if (base.isEmpty() || used->contains(base.toLower()))
        base = QString::number(id) + QLatin1Char('.') + suffixOf(originalName);
    while (used->contains(base.toLower()))
        base.prepend(QLatin1Char('_'));
    used->insert(base.toLower());
    return QStringLiteral("sources/") + base;
}

QJsonObject boxToJson(const BoundingBox &box)
{
    QJsonObject object{
        {"text", box.text},
        {"correctedText", box.correctedText},
        {"checkStatus", int(box.checkStatus)},
        {"correctedCheckStatus", int(box.correctedCheckStatus)},
        {"fixStatus", int(box.fixStatus)},
        {"useCorrectedText", box.useCorrectedText},
        {"label", box.label},
        {"x", box.rect.x()},
        {"y", box.rect.y()},
        {"w", box.rect.width()},
        {"h", box.rect.height()},
        {"positioned", box.positioned},
        {"duplicateSuspect", box.duplicateSuspect},
    };
    if (box.confidence >= 0.0)
        object.insert("confidence", box.confidence);
    if (box.correctedConfidence >= 0.0)
        object.insert("correctedConfidence", box.correctedConfidence);
    // What is wrong with a formula's LaTeX (ADR 194): written only when there
    // is something, so a project without rejected formulas stays as small as
    // before.
    if (box.syntaxProblem != LatexText::Problem::None)
        object.insert("syntaxProblem", int(box.syntaxProblem));
    // The verdict an approval replaced (ADR 187): written only when there is
    // one, so a project without approvals stays as small as before.
    if (box.checkStatusBeforeApproval != BoxCheckStatus::NotChecked)
        object.insert("checkStatusBeforeApproval", int(box.checkStatusBeforeApproval));
    if (box.correctedCheckStatusBeforeApproval != BoxCheckStatus::NotChecked)
        object.insert("correctedCheckStatusBeforeApproval", int(box.correctedCheckStatusBeforeApproval));
    return object;
}

BoundingBox boxFromJson(const QJsonObject &object, int schemaVersion)
{
    BoundingBox box;
    box.text = object.value("text").toString();
    box.correctedText = object.value("correctedText").toString();
    box.checkStatus = boxCheckStatusFromInt(object.value("checkStatus").toInt(int(BoxCheckStatus::NotChecked)));
    if (schemaVersion >= 2)
        box.correctedCheckStatus = boxCheckStatusFromInt(object.value("correctedCheckStatus").toInt(int(BoxCheckStatus::NotChecked)));
    // The fix outcome and the layer toggle are optional: a project written
    // before them reads the defaults (no fix ran, the correction is in view).
    box.fixStatus = boxFixStatusFromInt(object.value("fixStatus").toInt(int(BoxFixStatus::None)));
    box.useCorrectedText = object.value("useCorrectedText").toBool(true);
    box.label = object.value("label").toString();
    box.rect = QRectF(object.value("x").toDouble(), object.value("y").toDouble(), object.value("w").toDouble(), object.value("h").toDouble());
    box.positioned = object.value("positioned").toBool(true);
    box.duplicateSuspect = object.value("duplicateSuspect").toBool(false);
    box.confidence = object.value("confidence").toDouble(-1.0);
    box.correctedConfidence = object.value("correctedConfidence").toDouble(-1.0);
    // What is wrong with a formula's LaTeX (ADR 194): a project written before
    // it reads the default (nothing is wrong).
    box.syntaxProblem = LatexText::problemFromInt(object.value("syntaxProblem").toInt(int(LatexText::Problem::None)));
    // Optional: a project written before the undo of an approval reads the
    // default (nothing to put back).
    box.checkStatusBeforeApproval = boxCheckStatusFromInt(object.value("checkStatusBeforeApproval").toInt(int(BoxCheckStatus::NotChecked)));
    box.correctedCheckStatusBeforeApproval = boxCheckStatusFromInt(object.value("correctedCheckStatusBeforeApproval").toInt(int(BoxCheckStatus::NotChecked)));
    return box;
}

bool encodePng(const QImage &image, QByteArray *bytes)
{
    QBuffer buffer(bytes);
    if (!buffer.open(QIODevice::WriteOnly))
        return false;
    return image.save(&buffer, "PNG");
}

}  // namespace

QString ProjectStore::sourceTypeKey(DocumentSource type)
{
    switch (type) {
    case DocumentSource::Pdf:
        return QStringLiteral("pdf");
    case DocumentSource::DjVu:
        return QStringLiteral("djvu");
    case DocumentSource::Image:
        break;
    }
    return QStringLiteral("image");
}

bool ProjectStore::save(const QString &path, const ProjectData &data, QString *error)
{
    const QString tmpPath = path + QStringLiteral(".tmp");
    if (QFile::exists(tmpPath) && !QFile::remove(tmpPath)) {
        if (error)
            *error = QCoreApplication::translate("ProjectStore", "Cannot overwrite %1.").arg(tmpPath);
        return false;
    }

    QJsonArray sourcesJson;
    QSet<QString> usedNames;
    QStringList entryNames;
    entryNames.reserve(data.sources.size());
    for (const ProjectSource &source : data.sources) {
        const QString entry = assignEntryName(source.id, source.originalName, &usedNames);
        entryNames.append(entry);
        sourcesJson.append(QJsonObject{
            {"id", source.id},
            {"file", entry},
            {"name", source.originalName},
            {"type", source.typeName},
        });
    }
    QJsonArray pagesJson;
    for (const ProjectPageData &page : data.pages) {
        QJsonArray boxes;
        for (const BoundingBox &box : page.boxes)
            boxes.append(boxToJson(box));
        pagesJson.append(QJsonObject{
            {"sourceId", page.sourceId},
            {"sourcePageIndex", page.sourcePageIndex},
            {"recognized", page.recognized},
            {"recognitionMode", int(page.recognitionMode)},
            {"edited", page.edited},
            {"hasDuplicates", page.hasDuplicates},
            {"rotation", page.rotation},
            {"userOriented", page.userOriented},
            {"orientationProblem", int(page.orientationProblem)},
            {"text", page.text},
            {"baseline", page.baseline},
            {"parseNote", page.parseNote},
            {"boxes", boxes},
        });
    }
    const QJsonObject root{
        {"format", kFormatId.toString()},
        {"version", kSchemaVersion},
        {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"currentPage", data.currentPage},
        {"sources", sourcesJson},
        {"pages", pagesJson},
    };
    const QByteArray projectJson = QJsonDocument(root).toJson(QJsonDocument::Indented);

    QString zipError;
    ZipWriter zip(tmpPath, &zipError);
    if (!zipError.isEmpty()) {
        if (error)
            *error = zipError;
        return false;
    }

    for (int i = 0; i < data.sources.size(); ++i) {
        const ProjectSource &source = data.sources.at(i);
        const QString entry = entryNames.at(i);
        bool added = false;
        if (!source.sourcePath.isEmpty()) {
            QFile file(source.sourcePath);
            if (!file.open(QIODevice::ReadOnly)) {
                if (error)
                    *error = QCoreApplication::translate("ProjectStore", "Cannot read %1.").arg(source.sourcePath);
                return false;
            }
            added = zip.addEntry(entry, file);
        } else {
            QByteArray png;
            if (!encodePng(source.fallbackImage, &png)) {
                if (error)
                    *error = QCoreApplication::translate("ProjectStore", "Cannot encode the image of %1.").arg(source.originalName);
                return false;
            }
            added = zip.addEntry(entry, png);
        }
        if (!added) {
            if (error)
                *error = zip.error();
            return false;
        }
    }
    if (!zip.addEntry(kProjectEntry.toString(), projectJson)) {
        if (error)
            *error = zip.error();
        return false;
    }
    if (!zip.finish(&zipError)) {
        if (error)
            *error = zipError;
        return false;
    }

    if (QFile::exists(path) && !QFile::remove(path)) {
        if (error)
            *error = QCoreApplication::translate("ProjectStore", "Cannot overwrite %1.").arg(path);
        return false;
    }
    if (!QFile::rename(tmpPath, path)) {
        if (error)
            *error = QCoreApplication::translate("ProjectStore", "Cannot write %1.").arg(path);
        return false;
    }
    return true;
}

ProjectStore::LoadResult ProjectStore::load(const QString &path, const QString &extractDir)
{
    LoadResult result;

    const ExtractResult extracted = ArchiveExtractor::extractZip(path, extractDir);
    if (!extracted.ok) {
        result.error = extracted.error;
        return result;
    }
    if (!extracted.warning.isEmpty())
        result.warnings.append(extracted.warning);

    QFile manifest(QDir(extractDir).filePath(kProjectEntry.toString()));
    if (!manifest.open(QIODevice::ReadOnly)) {
        result.error = QCoreApplication::translate("ProjectStore", "The project has no manifest.");
        return result;
    }
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    if (!document.isObject()) {
        result.error = QCoreApplication::translate("ProjectStore", "The project manifest is malformed.");
        return result;
    }
    const QJsonObject root = document.object();
    if (root.value("format").toString() != kFormatId.toString()) {
        result.error = QCoreApplication::translate("ProjectStore", "%1 is not an LLocr project.").arg(QFileInfo(path).fileName());
        return result;
    }
    const int version = root.value("version").toInt(-1);
    if (version < 1 || version > kSchemaVersion) {
        result.error = QCoreApplication::translate("ProjectStore", "The project format version (%1) is not supported by this version of the app.").arg(version);
        return result;
    }

    QHash<int, int> sourceIndexById;
    const QJsonArray sources = root.value("sources").toArray();
    for (const QJsonValue &value : sources) {
        const QJsonObject object = value.toObject();
        ProjectSource source;
        source.id = object.value("id").toInt(-1);
        source.originalName = object.value("name").toString();
        source.typeName = object.value("type").toString(QStringLiteral("image"));
        const QString file = object.value("file").toString();
        source.extractedPath = containedPath(extractDir, file);
        if (source.extractedPath.isEmpty()) {
            source.available = false;
            if (!file.isEmpty()) {
                result.warnings.append(QCoreApplication::translate("ProjectStore", "The project points at a file outside its own folder (%1) — it was skipped.").arg(file));
            }
        } else {
            source.available = QFileInfo::exists(source.extractedPath);
            if (!source.available)
                result.warnings.append(QCoreApplication::translate("ProjectStore", "Embedded file %1 is missing from the project.").arg(file));
        }
        sourceIndexById.insert(source.id, result.data.sources.size());
        result.data.sources.append(source);
    }

    const QJsonArray pages = root.value("pages").toArray();
    for (const QJsonValue &value : pages) {
        const QJsonObject object = value.toObject();
        ProjectPageData page;
        page.sourceId = object.value("sourceId").toInt(-1);
        page.sourcePageIndex = object.value("sourcePageIndex").toInt(-1);
        page.recognized = object.value("recognized").toBool(false);
        page.edited = object.value("edited").toBool(false);
        page.hasDuplicates = object.value("hasDuplicates").toBool(false);
        // The turn of a page written before the field: the page stands as its
        // source does.
        page.rotation = normalizeRotation(object.value("rotation").toInt(0));
        page.userOriented = object.value("userOriented").toBool(false);
        // A project written before the flag: the check finds the page again.
        page.orientationProblem = PageOrientationProblem(object.value("orientationProblem").toInt(0));
        page.text = object.value("text").toString();
        page.baseline = object.value("baseline").toString();
        page.parseNote = object.value("parseNote").toString();
        const QJsonArray boxes = object.value("boxes").toArray();
        for (const QJsonValue &boxValue : boxes)
            page.boxes.append(boxFromJson(boxValue.toObject(), version));
        // v1 projects (and files written before the field) carry no mode:
        // derive it from what such a save can tell.
        const int storedMode = object.value("recognitionMode").toInt(-1);
        page.recognitionMode = storedMode >= int(PageRecognitionMode::MarkupOnly) && storedMode <= int(PageRecognitionMode::MarkupOcr) ? PageRecognitionMode(storedMode)
                               : page.recognized ? (page.boxes.isEmpty() ? PageRecognitionMode::RawOcr : PageRecognitionMode::MarkupOcr)
                                                 : PageRecognitionMode::None;
        result.data.pages.append(page);
    }
    result.data.currentPage = root.value("currentPage").toInt(0);
    result.error.clear();
    return result;
}

}  // namespace llocr
