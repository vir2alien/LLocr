#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "app/DocumentModel.h"
#include "app/ProjectStore.h"
#include "core/BoxTransitions.h"
#include "core/ModelProfiles.h"
#include "models/CropScaling.h"
#include "parsers/BlockStyle.h"

using namespace llocr;

// Dumps the block crops of a .llocr project exactly as the fix and the read
// tasks send them to the model: the page render (source pixels, the turn and
// the straightening), the block's rect crop (AppController::croppedImage's
// math) and the profile's crop-preprocessing stages (ADR 287) of the chosen
// model's fix role — both remedies share that pipeline and its policy, and
// the PNG bytes are what encodeImageDataUrl puts on the wire. Helper for the
// manual stand's crop folder (OCRtest/manual/crops), like
// candidate_render_dump is for the decision stand.
//
//   remedy_crop_dump --project corpus.llocr --out <dir> [--model-id qwen3.5-4b]

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOptions({{"project", "The .llocr project to read", "path"},
                    {"out", "Output directory for the crops and the manifest", "dir"},
                    {"model-id", "The fix role whose crop policy applies (default qwen3.5-4b)", "id"}});
    cli.process(app);

    const QString projectPath = cli.value("project");
    const QString outDir = cli.value("out");
    const QString modelId = cli.value("model-id").isEmpty() ? QStringLiteral("qwen3.5-4b") : cli.value("model-id");
    if (projectPath.isEmpty() || outDir.isEmpty()) {
        qCritical("--project and --out are required");
        return 2;
    }

    const ModelProfiles::Role *role = ModelProfiles::roleFor(modelId, QStringLiteral("fix"));
    if (!role) {
        qCritical("model %s carries no fix role", qPrintable(modelId));
        return 2;
    }
    const CropScalingPolicy policy = policyForRole(role);

    QDir().mkpath(outDir);
    QTemporaryDir extract;
    const ProjectStore::LoadResult load = ProjectStore::load(projectPath, extract.path());
    if (!load.error.isEmpty()) {
        qCritical("project load failed: %s", qPrintable(load.error));
        return 2;
    }

    // The project's pages map onto the document in project order: a source's
    // pages append once, at their first use — the restore rule of
    // ProjectController.
    DocumentModel document;
    QHash<int, int> documentIndexOfPage;
    QHash<int, int> documentBaseOfSource;
    for (int pi = 0; pi < load.data.pages.size(); ++pi) {
        const ProjectPageData &page = load.data.pages.at(pi);
        int sourceIndex = -1;
        for (int si = 0; si < load.data.sources.size(); ++si) {
            if (load.data.sources.at(si).id == page.sourceId) {
                sourceIndex = si;
                break;
            }
        }
        if (sourceIndex < 0 || !load.data.sources.at(sourceIndex).available) {
            qWarning("page %d: its source is missing, skipped", pi + 1);
            continue;
        }
        if (!documentBaseOfSource.contains(sourceIndex)) {
            QString appendError;
            if (!document.appendFile(load.data.sources.at(sourceIndex).extractedPath, &appendError)) {
                qWarning("page %d: %s", pi + 1, qPrintable(appendError));
                continue;
            }
            documentBaseOfSource.insert(sourceIndex, document.pageCount() - 1);
        }
        const int slot = page.sourcePageIndex >= 0 ? page.sourcePageIndex : 0;
        documentIndexOfPage.insert(pi, documentBaseOfSource.value(sourceIndex) + slot);
    }

    QJsonArray entries;
    int dumped = 0;
    int scaled = 0;
    for (int pi = 0; pi < load.data.pages.size(); ++pi) {
        const ProjectPageData &page = load.data.pages.at(pi);
        const int docIndex = documentIndexOfPage.value(pi, -1);
        if (docIndex < 0)
            continue;
        QString error;
        const QImage full = document.fullImage(docIndex, &error);
        if (full.isNull()) {
            qWarning("page %d: the render failed: %s", pi + 1, qPrintable(error));
            continue;
        }
        for (int bi = 0; bi < page.boxes.size(); ++bi) {
            const BoundingBox &box = page.boxes.at(bi);
            // The remedy scope (PageStatus::readableBoxes): every block except
            // the image placeholders and the user-owned ones — a block without
            // text is what the read without a recognition takes.
            if (blockStyleForLabel(box.label).style == BlockStyle::ImagePlaceholder)
                continue;
            const BoxCheckStatus ownership = BoxTransitions::effectiveCheckStatus(box);
            if (ownership == BoxCheckStatus::Edited || ownership == BoxCheckStatus::Approved)
                continue;
            const QString state = BoxTransitions::effectiveText(box);
            const QRectF norm = box.rect;
            if (norm.width() <= 0.0 || norm.height() <= 0.0)
                continue;
            QRect px(qRound(norm.x() * full.width()), qRound(norm.y() * full.height()), qRound(norm.width() * full.width()), qRound(norm.height() * full.height()));
            px = px.intersected(full.rect());
            if (px.width() < 1 || px.height() < 1)
                continue;
            const QImage crop = full.copy(px);

            const int glyphBefore = cropGlyphHeight(crop);
            const QImage wire = applyCropScaling(crop, box.label, policy);
            const int glyphAfter = cropGlyphHeight(wire);

            const QString name = QStringLiteral("p%1b%2.png").arg(pi).arg(bi);
            if (!wire.save(outDir + "/" + name, "PNG")) {
                qCritical("p%db%d: PNG save failed", pi, bi);
                return 3;
            }
            QJsonObject entry;
            entry.insert("key", QStringLiteral("p%1b%2").arg(pi).arg(bi));
            entry.insert("label", box.label);
            entry.insert("hasText", !state.isEmpty());
            entry.insert("crop", QString("%1x%2").arg(crop.width()).arg(crop.height()));
            entry.insert("wire", QString("%1x%2").arg(wire.width()).arg(wire.height()));
            entry.insert("glyphBefore", glyphBefore);
            entry.insert("glyphAfter", glyphAfter);
            entry.insert("scaled", wire.size() != crop.size());
            entries.append(entry);
            ++dumped;
            if (wire.size() != crop.size())
                ++scaled;
        }
        qInfo("page %d done (%d blocks)", pi + 1, page.boxes.size());
    }

    QJsonObject manifest;
    manifest.insert("project", projectPath);
    manifest.insert("modelId", modelId);
    manifest.insert("role", QStringLiteral("fix"));
    QJsonObject policyJson;
    for (auto it = policy.glyphHeightByBlock.constBegin(); it != policy.glyphHeightByBlock.constEnd(); ++it)
        policyJson.insert(it.key(), it.value());
    manifest.insert("glyphHeightByBlock", policyJson);
    manifest.insert("maxVisionTokens", policy.maxVisionTokens);
    manifest.insert("blocks", entries);
    manifest.insert("dumped", dumped);
    manifest.insert("scaled", scaled);
    QFile manifestFile(outDir + "/remedy_manifest.json");
    if (!manifestFile.open(QIODevice::WriteOnly) || manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0) {
        qCritical("manifest write failed");
        return 3;
    }
    qInfo("dumped %d crops (%d scaled by the profile) -> %s", dumped, scaled, qPrintable(outDir));
    return 0;
}
