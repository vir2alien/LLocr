#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "app/CandidateRenderer.h"
#include "app/ProjectStore.h"
#include "core/BoxTransitions.h"
#include "parsers/BlockRender.h"
#include "parsers/BlockStyle.h"
#include "parsers/ParserOptions.h"

using namespace llocr;

// Renders every text block of a .llocr project through the app's own
// CandidateRenderer and dumps the pictures next to a manifest for the
// decision-eval stand (the visual-diff calibration of docs/31 §6: the
// thresholds are measured against this app's own renders, ADR 184).
//
//   candidate_render_dump --project corpus.llocr --out <dir> --model-id mineru

int main(int argc, char *argv[])
{
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOptions({{"project", "The .llocr project to read", "path"},
                    {"out", "Output directory for renders and manifest.json", "dir"},
                    {"model-id", "Model id for the block style map (default mineru)", "id"},
                    {"skip-labels", "Comma-separated labels excluded from the check scope", "labels"}});
    cli.process(app);

    const QString projectPath = cli.value("project");
    const QString outDir = cli.value("out");
    const QString modelId = cli.value("model-id").isEmpty() ? QStringLiteral("mineru") : cli.value("model-id");
    QStringList skipLabels;
    for (const QString &label : cli.value("skip-labels").split(',', Qt::SkipEmptyParts))
        skipLabels << label.trimmed();
    if (skipLabels.isEmpty())
        skipLabels = QStringList{QStringLiteral("page_number"), QStringLiteral("header"), QStringLiteral("footer"), QStringLiteral("image")};

    if (projectPath.isEmpty() || outDir.isEmpty()) {
        qCritical("--project and --out are required");
        return 2;
    }
    QDir().mkpath(outDir);

    const ProjectStore::LoadResult load = ProjectStore::load(projectPath, outDir + "/extract");
    if (!load.error.isEmpty()) {
        qCritical("project load failed: %s", qPrintable(load.error));
        return 2;
    }

    ParserOptions options;
    options.modelId = modelId;
    options.keepPageNumbers = true;
    options.tablesAsHtml = false;

    CandidateRenderer renderer;
    QJsonArray entries;
    int rendered = 0;
    QElapsedTimer total;
    total.start();

    for (int pi = 0; pi < load.data.pages.size(); ++pi) {
        const ProjectPageData &page = load.data.pages.at(pi);
        for (int bi = 0; bi < page.boxes.size(); ++bi) {
            const BoundingBox &box = page.boxes.at(bi);
            if (skipLabels.contains(box.label))
                continue;
            const QString state = BoxTransitions::effectiveText(box);
            if (state.isEmpty())
                continue;
            const QString display = renderBlockText(state, blockStyleForLabel(box.label, modelId), options);

            QImage image;
            QString error;
            bool finished = false;
            renderer.render(display, [&](bool ok, const QImage &result, const QString &message) {
                if (ok)
                    image = result;
                else
                    error = message;
                finished = true;
            });
            QElapsedTimer timer;
            timer.start();
            while (!finished && timer.elapsed() < 60000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (!finished) {
                qCritical("p%db%d: render timed out", pi, bi);
                return 3;
            }
            if (image.isNull()) {
                qCritical("p%db%d: render failed: %s", pi, bi, qPrintable(error));
                return 3;
            }

            const QString name = QStringLiteral("p%1b%2.png").arg(pi).arg(bi);
            if (!image.save(outDir + "/" + name, "PNG")) {
                qCritical("p%db%d: PNG save failed", pi, bi);
                return 3;
            }
            QJsonObject entry;
            entry.insert("key", QStringLiteral("p%1b%2").arg(pi).arg(bi));
            entry.insert("label", box.label);
            entry.insert("state", state);
            entry.insert("display", display);
            entry.insert("render", name);
            entries.append(entry);
            ++rendered;
        }
        qInfo("page %d done (%d blocks, %d s)", pi, page.boxes.size(), int(total.elapsed() / 1000));
    }

    QJsonObject manifest;
    manifest.insert("project", projectPath);
    manifest.insert("modelId", modelId);
    manifest.insert("skipLabels", QJsonArray::fromStringList(skipLabels));
    manifest.insert("blocks", entries);
    QFile manifestFile(outDir + "/manifest.json");
    if (!manifestFile.open(QIODevice::WriteOnly) || manifestFile.write(QJsonDocument(manifest).toJson()) < 0) {
        qCritical("manifest write failed");
        return 3;
    }
    qInfo("rendered %d blocks -> %s", rendered, qPrintable(outDir));
    return 0;
}
