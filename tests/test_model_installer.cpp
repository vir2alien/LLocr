#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "config/SettingsStore.h"
#include "core/ByteSize.h"
#include "runtime/InstalledFilesModel.h"
#include "runtime/InstalledState.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/ModelInstaller.h"
#include "runtime/ModelQuantModel.h"
#include "runtime/ModelRegistry.h"
#include "runtime/RuntimeController.h"
#include "testsettings.h"

using namespace llocr;

namespace {

QString writeGguf(const QString &dir, const QString &name)
{
    const QString p = QDir(dir).filePath(name);
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write("GGUF placeholder");
    f.close();
    return p;
}

}  // namespace

// removeModel() semantics for multi-quant installs: a repo directory may hold
// several quants sharing one mmproj. Removing one quant must delete only its
// own file(s) and keep the shared projector while other quants remain; the
// folder (and the projector) go away only with the last quant.
class TestModelInstaller : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

    struct Setup {
        QTemporaryDir root;
        SettingsStore settings;
        // The launch-profile store and the runtime controller are created by
        // makeSetup()/the tests AFTER pointAtTempDir() — they read the runtime
        // dirs in their constructors.
        QScopedPointer<LaunchProfileStore> launchProfiles;
        QScopedPointer<RuntimeController> runtime;
        QScopedPointer<InstalledState> installed;
        QScopedPointer<ModelInstaller> installer;
    };

    static void makeRuntime(Setup &s)
    {
        // An empty built-in catalog (test binaries embed no resources).
        const QString presetsPath = QDir(s.root.path()).filePath(QStringLiteral("launch-presets.json"));
        {
            QFile f(presetsPath);
            if (f.open(QIODevice::WriteOnly))
                f.write(QByteArrayLiteral("{ \"schemaVersion\": 1, \"profiles\": [] }").constData());
        }
        s.launchProfiles.reset(new LaunchProfileStore(s.settings, presetsPath));
        s.installed.reset(new InstalledState(s.settings));
        s.runtime.reset(new RuntimeController(s.settings, *s.launchProfiles, s.installed.data()));
    }

    static void pointAtTempDir(SettingsStore &settings, const QString &root)
    {
        settings.setRuntimeRootDir(QDir(root).filePath(QStringLiteral("app")));
        settings.setRuntimeModelsDir(QDir(root).filePath(QStringLiteral("models")));
    }

    // Heap-allocated setup (Setup is non-copyable). Builds
    // models/org__repo/{model-Q4_K_M, model-Q8_0, mmproj-model-F16}, seeds the
    // index with both quant entries and constructs the installer (which loads
    // the index in its constructor). Returns nullptr on setup failure.
    static std::unique_ptr<Setup> makeMultiQuantSetup()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("org__repo"));
        if (!QDir().mkpath(sub))
            return nullptr;

        const QString q4Path = writeGguf(sub, QStringLiteral("model-Q4_K_M.gguf"));
        const QString q8Path = writeGguf(sub, QStringLiteral("model-Q8_0.gguf"));
        const QString mmproj = writeGguf(sub, QStringLiteral("mmproj-model-F16.gguf"));
        if (q4Path.isEmpty() || q8Path.isEmpty() || mmproj.isEmpty())
            return nullptr;

        ModelEntry q4;
        q4.id = QStringLiteral("org__repo_Q4_K_M");
        q4.title = QStringLiteral("org__repo");
        q4.repo = QStringLiteral("org/repo");
        q4.dir = sub;
        q4.modelPath = q4Path;
        q4.mmprojPath = mmproj;
        q4.origin = ModelOrigin::Managed;
        q4.quantization = QStringLiteral("Q4_K_M");
        q4.byteSize = 1024;

        ModelEntry q8 = q4;
        q8.id = QStringLiteral("org__repo_Q8_0");
        q8.modelPath = q8Path;
        q8.quantization = QStringLiteral("Q8_0");

        QString err;
        if (!ModelRegistry::save(modelsDir, {q4, q8}, err))
            return nullptr;

        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));
        return s;
    }

    // The models list is one row per model, its quantizations inside the row
    // (ADR 122); the helpers read it the way the QML delegate does.
    static QAbstractItemModel *ocrModels(ModelInstaller &installer) { return qobject_cast<QAbstractItemModel *>(installer.quantModels()); }

    static QAbstractItemModel *checkModels(ModelInstaller &installer) { return qobject_cast<QAbstractItemModel *>(installer.checkQuantModels()); }

    static ModelQuantModel *quantModel(QAbstractItemModel *model) { return qobject_cast<ModelQuantModel *>(model); }

    static QVariant roleAt(QAbstractItemModel *model, int row, int role) { return model->data(model->index(row, 0), role); }

    static int rowOfKey(QAbstractItemModel *model, const QString &key)
    {
        for (int i = 0; i < model->rowCount(); ++i) {
            if (roleAt(model, i, ModelQuantModel::KeyRole).toString() == key)
                return i;
        }
        return -1;
    }

    static QVariantMap quantOf(QAbstractItemModel *model, int row, const QString &id)
    {
        const QVariantList quants = roleAt(model, row, ModelQuantModel::QuantsRole).toList();
        for (const QVariant &value : quants) {
            const QVariantMap quant = value.toMap();
            if (quant.value(QStringLiteral("id")).toString().compare(id, Qt::CaseInsensitive) == 0)
                return quant;
        }
        return {};
    }

    // The file a row points at, read back through the installer's own list — the
    // row is a model, the registry index belongs to one of its quantizations.
    static QString pathOf(ModelInstaller &installer, const QString &key, const QString &role = QStringLiteral("pageOcr"))
    {
        auto *model = qobject_cast<ModelQuantModel *>(role == QLatin1String("blockRecognition") ? installer.checkQuantModels()
                                                      : role == QLatin1String("decision")       ? installer.decisionQuantModels()
                                                      : role == QLatin1String("layout")         ? installer.layoutQuantModels()
                                                      : role == QLatin1String("rawPageOcr")     ? installer.rawPageOcrQuantModels()
                                                      : role == QLatin1String("fix")            ? installer.fixQuantModels()
                                                                                                : installer.quantModels());
        const QList<int> indexes = model->entryIndexesFor(key);
        return indexes.isEmpty() ? QString() : installer.installedEntries().at(indexes.first()).modelPath;
    }

    // The installer's own index, which removeModel() and setActiveModel() address:
    // the list rows are filtered per role and are not registry indexes.
    static int registryIndexOfQuant(ModelInstaller &installer, const QString &quant)
    {
        const QList<ModelEntry> &entries = installer.installedEntries();
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.at(i).quantization == quant)
                return i;
        }
        return -1;
    }

    static QVariantMap choiceFor(const QVariantList &choices, const QString &task)
    {
        for (const QVariant &v : choices) {
            if (v.toMap().value(QStringLiteral("task")).toString() == task)
                return v.toMap();
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_model_installer"));
    }

    // A model whose file name carries no quant token (PaddleOCR-VL) must still
    // show a quant name everywhere the UI names one: the models list and the
    // "files on disk" list both read it off the entry.
    void aModelWithoutAQuantTokenInItsNameIsStillNamed()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("PaddlePaddle__PaddleOCR-VL-1.6-GGUF"));
        QVERIFY(QDir().mkpath(sub));
        const QString model = writeGguf(sub, QStringLiteral("PaddleOCR-VL-1.6-GGUF.gguf"));
        QVERIFY(!model.isEmpty());

        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        // The registry names the quantization from the model profile.
        const QList<ModelEntry> &entries = s->installer->installedEntries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().quantization, QStringLiteral("F16"));

        // The "files on disk" list shows the name, not a dash.
        auto *files = qobject_cast<QAbstractItemModel *>(s->installer->installedFiles());
        QVERIFY(files);
        QCOMPARE(files->rowCount(), 1);
        QCOMPARE(files->data(files->index(0, 0), InstalledFilesModel::QuantRole).toString(), QStringLiteral("F16"));
    }

    // The user-facing contract: deleting one quant from a shared folder must
    // leave the other quant and the common mmproj untouched.
    void removingOneQuantKeepsSharedMmproj()
    {
        std::unique_ptr<Setup> s = makeMultiQuantSetup();
        QVERIFY(s != nullptr);
        const int idx = registryIndexOfQuant(*s->installer, QStringLiteral("Q4_K_M"));
        QVERIFY(idx >= 0);

        const QString sub = QDir(s->settings.runtimeModelsDir()).filePath(QStringLiteral("org__repo"));
        const QString q4Path = QDir(sub).filePath(QStringLiteral("model-Q4_K_M.gguf"));
        const QString q8Path = QDir(sub).filePath(QStringLiteral("model-Q8_0.gguf"));
        const QString mmproj = QDir(sub).filePath(QStringLiteral("mmproj-model-F16.gguf"));
        QVERIFY(QFile::exists(q4Path));
        QVERIFY(QFile::exists(q8Path));
        QVERIFY(QFile::exists(mmproj));

        QVERIFY(s->installer->removeModel(idx).isEmpty());
        QVERIFY(!QFile::exists(q4Path));
        QVERIFY(QFile::exists(q8Path));
        QVERIFY(QFile::exists(mmproj));
        QVERIFY(QDir(sub).exists());

        QString err;
        bool rebuilt = false;
        const QList<ModelEntry> entries = ModelRegistry::load(s->settings.runtimeModelsDir(), rebuilt, err);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.at(0).quantization, QStringLiteral("Q8_0"));
        QCOMPARE(entries.at(0).modelPath, q8Path);
        QCOMPARE(entries.at(0).mmprojPath, mmproj);
    }

    // Removing the LAST quant removes the whole folder including the projector.
    void removingLastQuantRemovesFolderAndMmproj()
    {
        std::unique_ptr<Setup> s = makeMultiQuantSetup();
        QVERIFY(s != nullptr);
        const int q4 = registryIndexOfQuant(*s->installer, QStringLiteral("Q4_K_M"));
        QVERIFY(q4 >= 0);
        QVERIFY(s->installer->removeModel(q4).isEmpty());
        const int q8 = registryIndexOfQuant(*s->installer, QStringLiteral("Q8_0"));
        QVERIFY(q8 >= 0);
        QVERIFY(s->installer->removeModel(q8).isEmpty());

        const QString sub = QDir(s->settings.runtimeModelsDir()).filePath(QStringLiteral("org__repo"));
        QVERIFY(!QDir(sub).exists());
    }

    // A quant with its OWN (unshared) projector: removing it must delete both.
    void removingQuantRemovesUnsharedMmproj()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        const QString modelsDir = QDir(root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("org__repo"));
        QVERIFY(QDir().mkpath(sub));

        const QString q4Path = writeGguf(sub, QStringLiteral("model-Q4_K_M.gguf"));
        const QString q8Path = writeGguf(sub, QStringLiteral("model-Q8_0.gguf"));
        const QString mmprojA = writeGguf(sub, QStringLiteral("mmproj-A.gguf"));
        const QString mmprojB = writeGguf(sub, QStringLiteral("mmproj-B.gguf"));
        QVERIFY(!q4Path.isEmpty() && !q8Path.isEmpty() && !mmprojA.isEmpty() && !mmprojB.isEmpty());

        ModelEntry q4;
        q4.id = QStringLiteral("org__repo_Q4_K_M");
        q4.title = QStringLiteral("org__repo");
        q4.repo = QStringLiteral("org/repo");
        q4.dir = sub;
        q4.modelPath = q4Path;
        q4.mmprojPath = mmprojA;
        q4.origin = ModelOrigin::Managed;
        q4.quantization = QStringLiteral("Q4_K_M");
        q4.byteSize = 1024;

        ModelEntry q8 = q4;
        q8.id = QStringLiteral("org__repo_Q8_0");
        q8.modelPath = q8Path;
        q8.mmprojPath = mmprojB;
        q8.quantization = QStringLiteral("Q8_0");

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {q4, q8}, err), qPrintable(err));

        // An empty built-in catalog (test binaries embed no resources).
        const QString presetsPath = QDir(root.path()).filePath(QStringLiteral("launch-presets.json"));
        {
            QFile f(presetsPath);
            if (f.open(QIODevice::WriteOnly))
                f.write(QByteArrayLiteral("{ \"schemaVersion\": 1, \"profiles\": [] }").constData());
        }
        LaunchProfileStore launchProfiles(settings, presetsPath);
        RuntimeController runtime(settings, launchProfiles);
        InstalledState installed(settings);
        ModelInstaller installer(settings, runtime, installed);
        const int idx = registryIndexOfQuant(installer, q4.quantization);
        QVERIFY(idx >= 0);
        QVERIFY(installer.removeModel(idx).isEmpty());

        QVERIFY(!QFile::exists(q4Path));
        QVERIFY(!QFile::exists(mmprojA));
        QVERIFY(QFile::exists(q8Path));
        QVERIFY(QFile::exists(mmprojB));
    }

    // One row per model, carrying the quantizations the profile offers. What is
    // installed flips only its own quantization, never its sibling's, and the row
    // the models dialog shows is the row the action button acts on.
    void quantRowsTrackInstalledQuantizations()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("sahilchachra__Unlimited-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        const QString q8Path = writeGguf(sub, QStringLiteral("Unlimited-OCR-Q8_0.gguf"));
        const QString mmproj = writeGguf(sub, QStringLiteral("mmproj-Unlimited-OCR-F16.gguf"));
        QVERIFY(!q8Path.isEmpty() && !mmproj.isEmpty());

        ModelEntry q8;
        q8.id = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF_Q8_0");
        q8.title = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF");
        q8.repo = QStringLiteral("sahilchachra/Unlimited-OCR-GGUF");
        q8.dir = sub;
        q8.modelPath = q8Path;
        q8.mmprojPath = mmproj;
        q8.origin = ModelOrigin::Managed;
        q8.quantization = QStringLiteral("Q8_0");
        q8.byteSize = 1024;

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {q8}, err), qPrintable(err));

        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));
        ModelInstaller &mi = *s->installer;

        auto *models = ocrModels(mi);
        const int row = rowOfKey(models, QStringLiteral("unlimited-ocr"));
        QVERIFY(row >= 0);
        QCOMPARE(roleAt(models, row, ModelQuantModel::TitleRole).toString(), QStringLiteral("Unlimited-OCR"));
        QCOMPARE(roleAt(models, row, ModelQuantModel::SubtitleRole).toString(), QStringLiteral("sahilchachra/Unlimited-OCR-GGUF"));
        QVERIFY(roleAt(models, row, ModelQuantModel::ProfileRole).toBool());

        // The quants come from the profile, in the order the profile lists them.
        const QVariantList quants = roleAt(models, row, ModelQuantModel::QuantsRole).toList();
        const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), QStringLiteral("unlimited-ocr"));
        QVERIFY(profile && profile->files.quants.size() >= 2);
        QCOMPARE(quants.size(), profile->files.quants.size());
        QCOMPARE(quants.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("Q8_0"));
        QVERIFY(quants.at(0).toMap().value(QStringLiteral("installed")).toBool());
        QVERIFY(quants.at(0).toMap().value(QStringLiteral("downloadable")).toBool());
        const QVariantMap q4 = quantOf(models, row, QStringLiteral("Q4_K_M"));
        QVERIFY(!q4.isEmpty());
        QVERIFY(!q4.value(QStringLiteral("installed")).toBool());
        QVERIFY(q4.value(QStringLiteral("downloadable")).toBool());

        // The action button reads the *selected* quantization, which defaults to
        // the one that is installed; its size is the one measured on disk.
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q8_0"));
        QVERIFY(roleAt(models, row, ModelQuantModel::SelectedInstalledRole).toBool());
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedSizeRole).toLongLong(), QFileInfo(q8Path).size());

        QVERIFY(mi.removeQuant(QStringLiteral("unlimited-ocr"), QStringLiteral("Q8_0")).isEmpty());
        QVERIFY(!quantOf(models, row, QStringLiteral("Q8_0")).value(QStringLiteral("installed")).toBool());
        QVERIFY(!quantOf(models, row, QStringLiteral("Q4_K_M")).value(QStringLiteral("installed")).toBool());
    }

    // The recommendation the profile marks as default comes first, and every model
    // of the role is listed even with nothing installed: the list is what the user
    // picks from, not a record of what is already on disk.
    void quantRowsListTheModelsOfTheRole()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        auto *ocr = ocrModels(*s->installer);
        auto *check = checkModels(*s->installer);
        auto *decision = qobject_cast<ModelQuantModel *>(s->installer->decisionQuantModels());
        auto *rawPage = qobject_cast<ModelQuantModel *>(s->installer->rawPageOcrQuantModels());
        auto *fix = qobject_cast<ModelQuantModel *>(s->installer->fixQuantModels());

        QStringList ocrKeys;
        for (int i = 0; i < ocr->rowCount(); ++i)
            ocrKeys.append(roleAt(ocr, i, ModelQuantModel::KeyRole).toString());
        QCOMPARE(ocrKeys, QStringList({QStringLiteral("unlimited-ocr"), QStringLiteral("chandra-ocr-2"), QStringLiteral("lfm25-vl-3b"), QStringLiteral("lightonocr")}));

        QStringList checkKeys;
        for (int i = 0; i < check->rowCount(); ++i)
            checkKeys.append(roleAt(check, i, ModelQuantModel::KeyRole).toString());
        QCOMPARE(
            checkKeys,
            QStringList({QStringLiteral("glm-ocr"), QStringLiteral("lfm25-vl-3b"), QStringLiteral("lightonocr"), QStringLiteral("mineru"), QStringLiteral("paddleocr-vl"), QStringLiteral("teleocr")}));

        // The decision list serves the decision task only; the default model
        // leads, the rest sort by title (ModelProfiles::forRole).
        QStringList decisionKeys;
        for (int i = 0; i < decision->rowCount(); ++i)
            decisionKeys.append(roleAt(decision, i, ModelQuantModel::KeyRole).toString());
        QCOMPARE(decisionKeys, QStringList({QStringLiteral("clef-flash"), QStringLiteral("d3-flash"), QStringLiteral("d3-mini")}));
        QVERIFY(pathOf(*s->installer, QStringLiteral("clef-flash"), QStringLiteral("decision")).isEmpty());

        // The layout list serves the layout task only (ADR 150): two shipped
        // profiles declare it.
        auto *layout = qobject_cast<ModelQuantModel *>(s->installer->layoutQuantModels());
        QVERIFY(layout);
        QStringList layoutKeys;
        for (int i = 0; i < layout->rowCount(); ++i)
            layoutKeys.append(roleAt(layout, i, ModelQuantModel::KeyRole).toString());
        QCOMPARE(layoutKeys, QStringList({QStringLiteral("teleocr"), QStringLiteral("mineru")}));
        QVERIFY(pathOf(*s->installer, QStringLiteral("teleocr"), QStringLiteral("layout")).isEmpty());

        // The two tasks without a settings window of their own (rawPageOcr,
        // fix) still expose their lists — the task window (stage 6) reads them.
        QStringList rawPageKeys;
        for (int i = 0; i < rawPage->rowCount(); ++i)
            rawPageKeys.append(roleAt(rawPage, i, ModelQuantModel::KeyRole).toString());
        QCOMPARE(rawPageKeys, QStringList({QStringLiteral("glm-ocr"), QStringLiteral("chandra-ocr-2"), QStringLiteral("lightonocr")}));

        QStringList fixKeys;
        for (int i = 0; i < fix->rowCount(); ++i)
            fixKeys.append(roleAt(fix, i, ModelQuantModel::KeyRole).toString());
        QCOMPARE(fixKeys, QStringList({QStringLiteral("qwen3.5-4b"), QStringLiteral("gemma-4-12b"), QStringLiteral("gemma-4-e4b"), QStringLiteral("qwen3.5-9b")}));

        // TeleOCR answers two tasks and offers the quantizations its profile
        // declares — the shipped file is the source here, not a copy of it, so an
        // edit to the catalog is what this follows.
        const int teleocr = rowOfKey(check, QStringLiteral("teleocr"));
        QVERIFY(teleocr >= 0);
        const ModelProfiles::Profile *teleocrProfile = ModelProfiles::find(ModelProfiles::instance(), QStringLiteral("teleocr"));
        QVERIFY(teleocrProfile);
        const QVariantList quants = roleAt(check, teleocr, ModelQuantModel::QuantsRole).toList();
        QCOMPARE(quants.size(), teleocrProfile->files.quants.size());
        QCOMPARE(quants.first().toMap().value(QStringLiteral("id")).toString(), teleocrProfile->files.quants.first().id.toUpper());
        QVERIFY(!roleAt(check, teleocr, ModelQuantModel::SelectedInstalledRole).toBool());
        QVERIFY(roleAt(check, teleocr, ModelQuantModel::SelectedDownloadableRole).toBool());
    }

    // A quantization the profile does not list — hand-downloaded, or left behind
    // by a catalog that has moved on — stays in the row, or the file on disk would
    // have no row to be activated or deleted from.
    void quantRowsKeepQuantizationsTheProfileDoesNotList()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("sahilchachra__Unlimited-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        const QString oddPath = writeGguf(sub, QStringLiteral("Unlimited-OCR-Q4_0.gguf"));
        QVERIFY(!oddPath.isEmpty());

        ModelEntry odd;
        odd.id = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF_Q4_0");
        odd.title = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF");
        odd.repo = QStringLiteral("sahilchachra/Unlimited-OCR-GGUF");
        odd.dir = sub;
        odd.modelPath = oddPath;
        odd.origin = ModelOrigin::Managed;
        odd.quantization = QStringLiteral("Q4_0");
        odd.roles = {QStringLiteral("pageOcr")};

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {odd}, err), qPrintable(err));

        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        auto *models = ocrModels(*s->installer);
        const int row = rowOfKey(models, QStringLiteral("unlimited-ocr"));
        QVERIFY(row >= 0);
        const QVariantMap extra = quantOf(models, row, QStringLiteral("Q4_0"));
        QVERIFY(!extra.isEmpty());
        QVERIFY(extra.value(QStringLiteral("installed")).toBool());
        // Nothing in the profile points at it, so it cannot be downloaded again.
        QVERIFY(!extra.value(QStringLiteral("downloadable")).toBool());

        // A model the user brought themselves gets a row of its own, with no
        // quantization picker and no deletion (the registry refuses both).
        const QString elsewhere = QDir(s->root.path()).filePath(QStringLiteral("elsewhere"));
        QVERIFY(QDir().mkpath(elsewhere));
        const QString externalPath = writeGguf(elsewhere, QStringLiteral("my-notes.gguf"));
        QVERIFY(!externalPath.isEmpty());
        ModelEntry external;
        external.id = QStringLiteral("outside_model");
        external.modelPath = externalPath;
        external.dir = elsewhere;
        external.origin = ModelOrigin::External;
        external.roles = {QStringLiteral("pageOcr")};
        QVERIFY2(ModelRegistry::save(modelsDir, {odd, external}, err), qPrintable(err));

        s->installer->refreshInstalled();
        const int externalRow = rowOfKey(models, QStringLiteral("outside_model"));
        QVERIFY(externalRow >= 0);
        QVERIFY(!roleAt(models, externalRow, ModelQuantModel::ProfileRole).toBool());
        QCOMPARE(roleAt(models, externalRow, ModelQuantModel::QuantsRole).toList().size(), 1);
        QCOMPARE(roleAt(models, externalRow, ModelQuantModel::SubtitleRole).toString(), external.dir);
        QCOMPARE(pathOf(*s->installer, QStringLiteral("outside_model")), externalPath);
    }

    // The row shows the task's active quantization; a remembered pick (one per
    // profile, shared by every task riding it) is only the default while the
    // task is not riding one of the row's quants yet. Otherwise two tasks on
    // one profile would display each other's choice, and a stale remembered
    // value would outshine what the task actually runs.
    void selectedQuantFollowsTheActiveModel()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("sahilchachra__Unlimited-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));
        ModelInstaller &mi = *s->installer;
        auto *models = ocrModels(mi);
        const int row = rowOfKey(models, QStringLiteral("unlimited-ocr"));
        QVERIFY(row >= 0);

        // The settings are shared by every test in the run, so a pick left behind
        // by an earlier one must not decide what this row shows.
        s->settings.setSelectedQuant(QStringLiteral("unlimited-ocr"), QString());

        // Nothing installed and nothing chosen: the profile's first quantization.
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q8_0"));

        ModelEntry entry;
        entry.id = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF_Q4_K_M");
        entry.title = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF");
        entry.repo = QStringLiteral("sahilchachra/Unlimited-OCR-GGUF");
        entry.dir = sub;
        entry.modelPath = writeGguf(sub, QStringLiteral("Unlimited-OCR-Q4_K_M.gguf"));
        entry.origin = ModelOrigin::Managed;
        entry.quantization = QStringLiteral("Q4_K_M");
        entry.roles = {QStringLiteral("pageOcr")};

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {entry}, err), qPrintable(err));
        mi.refreshInstalled();

        // With a quantization on disk and nothing chosen, that one is shown.
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q4_K_M"));

        mi.selectQuant(QStringLiteral("unlimited-ocr"), QStringLiteral("Q8_0"));
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q8_0"));
        QCOMPARE(s->settings.selectedQuant(QStringLiteral("unlimited-ocr")), QStringLiteral("Q8_0"));

        // The task's active quantization outranks the remembered pick: the row
        // must show what the task runs, not the last profile-level pick.
        QVERIFY(mi.setActiveModel(quantModel(models)->entryIndexFor(QStringLiteral("unlimited-ocr"), QStringLiteral("Q4_K_M"))).isEmpty());
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q4_K_M"));
        QVERIFY(roleAt(models, row, ModelQuantModel::SelectedActiveRole).toBool());
        QVERIFY(roleAt(models, row, ModelQuantModel::ActiveRole).toBool());
        QVERIFY(roleAt(models, row, ModelQuantModel::SelectedInstalledRole).toBool());

        // A pick that is not in the profile is refused rather than silently applied.
        mi.selectQuant(QStringLiteral("unlimited-ocr"), QStringLiteral("Q2_K"));
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q4_K_M"));

        s->settings.setSelectedQuant(QStringLiteral("unlimited-ocr"), QString());
    }

    // Saving a choice that is already on disk activates it AND refreshes the
    // remembered pick — a stale saved value used to win the next open.
    void applyTaskChoicesRefreshesTheRememberedPick()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("sahilchachra__Unlimited-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        ModelEntry entry;
        entry.id = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF_Q4_K_M");
        entry.title = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF");
        entry.repo = QStringLiteral("sahilchachra/Unlimited-OCR-GGUF");
        entry.dir = sub;
        entry.modelPath = writeGguf(sub, QStringLiteral("Unlimited-OCR-Q4_K_M.gguf"));
        entry.mmprojPath = writeGguf(sub, QStringLiteral("mmproj-Unlimited-OCR-F16.gguf"));
        entry.origin = ModelOrigin::Managed;
        entry.quantization = QStringLiteral("Q4_K_M");
        QVERIFY(!entry.modelPath.isEmpty() && !entry.mmprojPath.isEmpty());

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {entry}, err), qPrintable(err));
        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));
        ModelInstaller &mi = *s->installer;

        s->settings.setSelectedQuant(QStringLiteral("unlimited-ocr"), QStringLiteral("Q8_0"));

        const QVariantList choices = {
            QVariantMap{{QStringLiteral("task"), QStringLiteral("pageOcr")}, {QStringLiteral("key"), QStringLiteral("unlimited-ocr")}, {QStringLiteral("quantId"), QStringLiteral("Q4_K_M")}}};
        const QVariantList missing = mi.applyTaskChoices(choices);
        QVERIFY(missing.isEmpty());
        QCOMPARE(s->settings.selectedQuant(QStringLiteral("unlimited-ocr")), QStringLiteral("Q4_K_M"));
        QCOMPARE(s->settings.launchModelPathForRole(QStringLiteral("pageOcr")), entry.modelPath);
    }

    // The wizard's auto-selection (ADR 169/170): every task gets its
    // role-default profile with the best quantization the budget allows.
    void defaultTaskChoicesFollowRoleDefaults()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        const QVariantList choices = s->installer->defaultTaskChoices(16LL << 40);
        QCOMPARE(choices.size(), 6);
        const QHash<QString, QPair<QString, QString>> expected = {
            {QStringLiteral("pageOcr"), {QStringLiteral("unlimited-ocr"), QStringLiteral("Q8_0")}},
            {QStringLiteral("rawPageOcr"), {QStringLiteral("glm-ocr"), QStringLiteral("Q8_0")}},
            {QStringLiteral("blockRecognition"), {QStringLiteral("glm-ocr"), QStringLiteral("Q8_0")}},
            {QStringLiteral("decision"), {QStringLiteral("clef-flash"), QStringLiteral("Q8_0")}},
            {QStringLiteral("layout"), {QStringLiteral("teleocr"), QStringLiteral("Q8_0")}},
            {QStringLiteral("fix"), {QStringLiteral("qwen3.5-4b"), QStringLiteral("Q8_0")}},
        };
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            const QVariantMap choice = choiceFor(choices, it.key());
            QCOMPARE(choice.value(QStringLiteral("key")).toString(), it.value().first);
            QCOMPARE(choice.value(QStringLiteral("quantId")).toString(), it.value().second);
        }
    }

    // A tight budget steps down the curated ladder; when nothing fits, the
    // smallest quantization still wins over a hopeless one.
    void defaultTaskChoicesFitTheMemoryBudget()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        const QVariantList choices = s->installer->defaultTaskChoices(2LL << 30);
        QCOMPARE(choiceFor(choices, QStringLiteral("pageOcr")).value(QStringLiteral("quantId")).toString(),
                 QStringLiteral("Q4_K_M"));  // q8_0/q6_k/q5_k_m all exceed 2 GiB
        QCOMPARE(choiceFor(choices, QStringLiteral("rawPageOcr")).value(QStringLiteral("quantId")).toString(), QStringLiteral("Q8_0"));
        QCOMPARE(choiceFor(choices, QStringLiteral("blockRecognition")).value(QStringLiteral("quantId")).toString(), QStringLiteral("Q8_0"));
        QCOMPARE(choiceFor(choices, QStringLiteral("decision")).value(QStringLiteral("quantId")).toString(),
                 QStringLiteral("Q4_0"));  // nothing of clef-flash fits → the smallest
        QCOMPARE(choiceFor(choices, QStringLiteral("layout")).value(QStringLiteral("quantId")).toString(), QStringLiteral("Q8_0"));
        QCOMPARE(choiceFor(choices, QStringLiteral("fix")).value(QStringLiteral("quantId")).toString(),
                 QStringLiteral("IQ4_XS"));  // nothing of qwen3.5-4b fits → the smallest
    }

    // A quant already on disk beats a better download: the wizard must not
    // re-download what the machine already has.
    void defaultTaskChoicesPreferTheInstalledQuant()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("sahilchachra__Unlimited-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        ModelEntry entry;
        entry.id = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF_Q4_K_M");
        entry.title = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF");
        entry.repo = QStringLiteral("sahilchachra/Unlimited-OCR-GGUF");
        entry.dir = sub;
        entry.modelPath = writeGguf(sub, QStringLiteral("Unlimited-OCR-Q4_K_M.gguf"));
        entry.mmprojPath = writeGguf(sub, QStringLiteral("mmproj-Unlimited-OCR-F16.gguf"));
        entry.origin = ModelOrigin::Managed;
        entry.quantization = QStringLiteral("Q4_K_M");
        QVERIFY(!entry.modelPath.isEmpty() && !entry.mmprojPath.isEmpty());

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {entry}, err), qPrintable(err));
        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        const QVariantList choices = s->installer->defaultTaskChoices(16LL << 40);
        QCOMPARE(choiceFor(choices, QStringLiteral("pageOcr")).value(QStringLiteral("quantId")).toString(), QStringLiteral("Q4_K_M"));
        QCOMPARE(choiceFor(choices, QStringLiteral("layout")).value(QStringLiteral("quantId")).toString(), QStringLiteral("Q8_0"));
    }

    // Deleting a model takes every quantization of it, but only after the whole
    // row has been checked: a model with half its files left is worse than one
    // that was not deleted at all.
    void deletingAModelTakesEveryQuantizationOfIt()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("sahilchachra__Unlimited-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        const QString q8Path = writeGguf(sub, QStringLiteral("Unlimited-OCR-Q8_0.gguf"));
        const QString q4Path = writeGguf(sub, QStringLiteral("Unlimited-OCR-Q4_K_M.gguf"));
        const QString mmproj = writeGguf(sub, QStringLiteral("mmproj-Unlimited-OCR-F16.gguf"));
        QVERIFY(!q8Path.isEmpty() && !q4Path.isEmpty() && !mmproj.isEmpty());

        ModelEntry q8;
        q8.id = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF_Q8_0");
        q8.title = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF");
        q8.repo = QStringLiteral("sahilchachra/Unlimited-OCR-GGUF");
        q8.dir = sub;
        q8.modelPath = q8Path;
        q8.mmprojPath = mmproj;
        q8.origin = ModelOrigin::Managed;
        q8.quantization = QStringLiteral("Q8_0");
        q8.roles = {QStringLiteral("pageOcr")};

        ModelEntry q4 = q8;
        q4.id = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF_Q4_K_M");
        q4.modelPath = q4Path;
        q4.quantization = QStringLiteral("Q4_K_M");

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {q8, q4}, err), qPrintable(err));

        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));
        ModelInstaller &mi = *s->installer;
        auto *models = ocrModels(mi);
        const int row = rowOfKey(models, QStringLiteral("unlimited-ocr"));
        QVERIFY(row >= 0);
        QCOMPARE(quantModel(models)->entryIndexesFor(QStringLiteral("unlimited-ocr")).size(), 2);

        QVERIFY(mi.removeModelRow(QStringLiteral("unlimited-ocr")).isEmpty());
        QVERIFY(!QFile::exists(q8Path));
        QVERIFY(!QFile::exists(q4Path));
        QVERIFY(!QFile::exists(mmproj));
        QCOMPARE(mi.installedCount(), 0);

        // The row stays, now offering both quantizations for download again.
        QCOMPARE(quantOf(models, row, QStringLiteral("Q8_0")).value(QStringLiteral("installed")).toBool(), false);
        QVERIFY(mi.removeModelRow(QStringLiteral("unlimited-ocr")).isEmpty() == false);
    }

    // The installed-model lists are role-filtered: a vision model goes to the
    // OCR window, a plain text model to the validator window; the role's
    // active model is always listed regardless of its recorded roles, and the
    // filtered index maps back to the full registry index for the actions.
    // Regression: parser/prompt are also recorded for models installed from
    // the validate catalog, so only mmproj proves an OCR model; and a legacy
    // entry active as the verifier must stay out of the OCR list.
    void installedListFilteredByRole()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        SettingsStore &settings = s->settings;
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("org__repo"));
        QVERIFY(QDir().mkpath(sub));

        const QString ocrPath = writeGguf(sub, QStringLiteral("ocr-Q4_K_M.gguf"));
        const QString mmproj = writeGguf(sub, QStringLiteral("mmproj-ocr-F16.gguf"));
        const QString textPath = writeGguf(sub, QStringLiteral("chat-Q4_K_M.gguf"));
        const QString verifierPath = writeGguf(sub, QStringLiteral("verify-Q4_K_M.gguf"));
        QVERIFY(!ocrPath.isEmpty() && !mmproj.isEmpty() && !textPath.isEmpty() && !verifierPath.isEmpty());

        ModelEntry ocr;
        ocr.id = QStringLiteral("org__repo_ocr");
        ocr.title = QStringLiteral("org__repo");
        ocr.repo = QStringLiteral("org/repo");
        ocr.dir = sub;
        ocr.modelPath = ocrPath;
        ocr.mmprojPath = mmproj;
        ocr.origin = ModelOrigin::Managed;
        ocr.quantization = QStringLiteral("Q4_K_M");
        ocr.byteSize = 1024;

        // A text model installed from the validate catalog: parser/prompt are
        // recorded (ADR 72 — both catalogs ship OCR data), roles are not.
        ModelEntry text = ocr;
        text.id = QStringLiteral("org__repo_chat");
        text.modelPath = textPath;
        text.mmprojPath.clear();
        text.quantization = QStringLiteral("chat");
        text.parser = QStringLiteral("det_tokens");

        // A vision-capable model used as the verifier (legacy, no roles).
        ModelEntry verifier = ocr;
        verifier.id = QStringLiteral("org__repo_verify");
        verifier.modelPath = verifierPath;
        verifier.quantization = QStringLiteral("verify");

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {ocr, text, verifier}, err), qPrintable(err));
        settings.setCheckLaunchModelPath(verifierPath);

        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));
        ModelInstaller &mi = *s->installer;

        // OCR list: only the mmproj model — the verifier (active as the check
        // model) and the parser-carrying text model stay out of it.
        auto *ocrList = ocrModels(mi);
        auto *checkList = checkModels(mi);
        QVERIFY(ocrList);
        QVERIFY(checkList);
        QCOMPARE(rowOfKey(ocrList, QStringLiteral("org__repo_ocr")), 4);
        QCOMPARE(rowOfKey(ocrList, QStringLiteral("org__repo_chat")), -1);
        QCOMPARE(rowOfKey(ocrList, QStringLiteral("org__repo_verify")), -1);
        QCOMPARE(rowOfKey(checkList, QStringLiteral("org__repo_chat")), 6);
        QCOMPARE(rowOfKey(checkList, QStringLiteral("org__repo_verify")), 7);
        QCOMPARE(pathOf(mi, QStringLiteral("org__repo_ocr")), ocrPath);
        QCOMPARE(pathOf(mi, QStringLiteral("org__repo_chat"), QStringLiteral("blockRecognition")), textPath);
        QCOMPARE(pathOf(mi, QStringLiteral("org__repo_verify"), QStringLiteral("blockRecognition")), verifierPath);

        // A quantization of a model the user brought resolves back to the
        // installer's own index, which is what activate / delete act on.
        QCOMPARE(quantModel(ocrList)->entryIndexFor(QStringLiteral("org__repo_ocr"), QStringLiteral("Q4_K_M")), 0);

        // The role's active model is always listed: the text model becomes the
        // active OCR model and must appear in the OCR list too.
        settings.setLaunchModelPath(textPath);
        QCOMPARE(rowOfKey(ocrList, QStringLiteral("org__repo_chat")), 5);
        QCOMPARE(rowOfKey(checkList, QStringLiteral("org__repo_chat")), 6);
    }

    // A list model with named roles: a QML delegate that misspells a role name
    // gets an empty cell at runtime instead of a compile error (ADR 115). The
    // rows must carry the values the delegate binds, and the model must not
    // reset when nothing changed.
    void quantRowsExposeNamedRoles()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString modelsDir = QDir(root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("org__repo"));
        QVERIFY(QDir().mkpath(sub));
        const QString modelPath = writeGguf(sub, QStringLiteral("ocr-Q4_K_M.gguf"));
        const QString mmproj = writeGguf(sub, QStringLiteral("mmproj-ocr-F16.gguf"));
        QVERIFY(!modelPath.isEmpty() && !mmproj.isEmpty());

        ModelEntry entry;
        entry.id = QStringLiteral("org__repo");
        entry.title = QStringLiteral("org/repo");
        entry.repo = QStringLiteral("org/repo");
        entry.dir = sub;
        entry.modelPath = modelPath;
        entry.mmprojPath = mmproj;
        entry.origin = ModelOrigin::Managed;
        entry.quantization = QStringLiteral("Q4_K_M");
        entry.byteSize = 4096;
        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {entry}, err), qPrintable(err));

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        LaunchProfileStore launchProfiles(settings);
        InstalledState installed(settings);
        RuntimeController runtime(settings, launchProfiles);
        ModelInstaller installer(settings, runtime, installed);

        auto *models = ocrModels(installer);
        QVERIFY(models);
        // The models of the task, and the one the user brought themselves.
        QCOMPARE(models->rowCount(), 5);

        const QHash<int, QByteArray> names = models->roleNames();
        QCOMPARE(names.value(ModelQuantModel::KeyRole), QByteArray("key"));
        QCOMPARE(names.value(ModelQuantModel::ActiveRole), QByteArray("active"));
        QCOMPARE(names.value(ModelQuantModel::SelectedLabelRole), QByteArray("selectedLabel"));

        const int row = rowOfKey(models, QStringLiteral("org__repo"));
        QVERIFY(row >= 0);
        QVERIFY(!roleAt(models, row, ModelQuantModel::ProfileRole).toBool());
        QCOMPARE(roleAt(models, row, ModelQuantModel::TitleRole).toString(), QStringLiteral("ocr Q4_K_M"));
        // The scan canonicalizes entry.dir (QTemporaryDir hands out /var/...,
        // a symlink to /private/var/... on macOS).
        QCOMPARE(roleAt(models, row, ModelQuantModel::SubtitleRole).toString(), QFileInfo(sub).canonicalFilePath());
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q4_K_M"));
        QVERIFY(roleAt(models, row, ModelQuantModel::SelectedInstalledRole).toBool());
        QVERIFY(!roleAt(models, row, ModelQuantModel::SelectedActiveRole).toBool());
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedSizeRole).toLongLong(), QFileInfo(modelPath).size());
        // A row outside the model has no data, rather than reading row 0.
        QVERIFY(!roleAt(models, 7, ModelQuantModel::KeyRole).isValid());

        // A rescan that finds the same models must not reset the model.
        QSignalSpy resetSpy(models, &QAbstractItemModel::modelReset);
        installer.refreshInstalled();
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(models->rowCount(), 5);

        // Activating the model re-reads the highlight from the settings.
        QVERIFY(installer.setActiveModel(0).isEmpty());
        QVERIFY(roleAt(models, row, ModelQuantModel::ActiveRole).toBool());
    }

    // The highlight is *derived* from the settings, so activating a model changes
    // no entry and — when both quantizations are visible in this role — not even
    // the row set. The view still has to be told, or the list keeps showing the
    // previous model as active until the window is rebuilt.
    void activatingAQuantizationRepaintsTheRows()
    {
        std::unique_ptr<Setup> s = makeMultiQuantSetup();
        QVERIFY(s != nullptr);

        // Both selections set explicitly: the tests share one QSettings
        // process-wide, so an inherited value would decide the initial highlight.
        s->settings.setCheckLaunchModelPath(QString());
        s->settings.setLaunchModelPath(QDir(QDir(s->settings.runtimeModelsDir()).filePath(QStringLiteral("org__repo"))).filePath(QStringLiteral("model-Q4_K_M.gguf")));

        auto *models = ocrModels(*s->installer);
        QVERIFY(models != nullptr);
        const int q4Row = rowOfKey(models, QStringLiteral("org__repo_Q4_K_M"));
        const int q8Row = rowOfKey(models, QStringLiteral("org__repo_Q8_0"));
        QVERIFY(q4Row >= 0);
        QVERIFY(q8Row >= 0);
        QVERIFY(roleAt(models, q4Row, ModelQuantModel::ActiveRole).toBool());
        QVERIFY(!roleAt(models, q8Row, ModelQuantModel::ActiveRole).toBool());

        QSignalSpy changed(models, &QAbstractItemModel::dataChanged);

        QVERIFY(s->installer->setActiveModel(quantModel(models)->entryIndexFor(QStringLiteral("org__repo_Q8_0"), QStringLiteral("Q8_0"))).isEmpty());

        QVERIFY(!roleAt(models, q4Row, ModelQuantModel::ActiveRole).toBool());
        QVERIFY(roleAt(models, q8Row, ModelQuantModel::ActiveRole).toBool());

        QVERIFY2(changed.count() > 0, "the list was not told to re-read the highlight");
        bool sawActiveRole = false;
        for (const QList<QVariant> &call : changed) {
            if (call.at(2).toList().contains(ModelQuantModel::ActiveRole))
                sawActiveRole = true;
        }
        QVERIFY2(sawActiveRole, "dataChanged carried no ActiveRole");
    }

    // A model that is selected but no longer on disk is *reported*, not silently
    // dropped: the file may live outside the models directory, and the user has
    // to decide what to do about it (ADR 116).
    void aSelectedModelThatVanishedIsReported()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString modelsDir = QDir(root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("org__repo"));
        QVERIFY(QDir().mkpath(sub));
        const QString modelPath = writeGguf(sub, QStringLiteral("ocr-Q4_K_M.gguf"));
        const QString mmproj = writeGguf(sub, QStringLiteral("mmproj-ocr-F16.gguf"));
        QVERIFY(!modelPath.isEmpty() && !mmproj.isEmpty());

        ModelEntry entry;
        entry.id = QStringLiteral("org__repo");
        entry.repo = QStringLiteral("org/repo");
        entry.dir = sub;
        entry.modelPath = modelPath;
        entry.mmprojPath = mmproj;
        entry.origin = ModelOrigin::Managed;
        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {entry}, err), qPrintable(err));

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        // Both model selections are set explicitly: the tests share one
        // QSettings process-wide, so an inherited check-model path would be
        // reported as stale before the test even starts.
        settings.setCheckLaunchModelPath(QString());
        settings.setLaunchModelPath(modelPath);
        LaunchProfileStore launchProfiles(settings);
        InstalledState installed(settings);
        RuntimeController runtime(settings, launchProfiles);
        ModelInstaller installer(settings, runtime, installed);
        QVERIFY2(!installer.statusMessage().contains(QStringLiteral("no longer")), qPrintable(installer.statusMessage()));

        QVERIFY(QDir(sub).removeRecursively());
        installer.refreshInstalled();

        QCOMPARE(installer.installedCount(), 0);
        QVERIFY2(installer.statusMessage().contains(QStringLiteral("no longer")), qPrintable(installer.statusMessage()));
        QVERIFY2(installer.statusMessage().contains(QStringLiteral("ocr-Q4_K_M.gguf")), qPrintable(installer.statusMessage()));
    }

    // Activating a model has to move the profile with it: the check model runs
    // with the launch parameters of the family it belongs to, and picking an
    // installed GGUF that no longer left the previous family's profile in place
    // is what put Unlimited-OCR's flags on a Qwen server.
    void activatingAModelSelectsItsFamilyProfile()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString modelsDir = QDir(root.path()).filePath(QStringLiteral("models"));

        ModelEntry qwen;
        const QString qwenDir = QDir(modelsDir).filePath(QStringLiteral("unsloth__Qwen3.5-4B-MTP-GGUF"));
        QVERIFY(QDir().mkpath(qwenDir));
        qwen.id = QStringLiteral("unsloth__Qwen3.5-4B-MTP-GGUF");
        qwen.repo = QStringLiteral("unsloth/Qwen3.5-4B-MTP-GGUF");
        qwen.dir = qwenDir;
        qwen.modelPath = writeGguf(qwenDir, QStringLiteral("Qwen3.5-4B-UD-Q4_K_XL.gguf"));
        qwen.mmprojPath = writeGguf(qwenDir, QStringLiteral("mmproj-F16.gguf"));
        qwen.origin = ModelOrigin::Managed;

        ModelEntry lfm = qwen;
        const QString lfmDir = QDir(modelsDir).filePath(QStringLiteral("LiquidAI__LFM2.5-VL-3B-GGUF"));
        QVERIFY(QDir().mkpath(lfmDir));
        lfm.id = QStringLiteral("LiquidAI__LFM2.5-VL-3B-GGUF");
        lfm.repo = QStringLiteral("LiquidAI/LFM2.5-VL-3B-GGUF");
        lfm.dir = lfmDir;
        lfm.modelPath = writeGguf(lfmDir, QStringLiteral("LFM2.5-VL-3B-Q8_0.gguf"));
        lfm.mmprojPath = writeGguf(lfmDir, QStringLiteral("mmproj-LFM2.5-VL-3B-F16.gguf"));

        QVERIFY(!qwen.modelPath.isEmpty() && !lfm.modelPath.isEmpty());
        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {qwen, lfm}, err), qPrintable(err));

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        settings.setFixRequestProfileId(QStringLiteral("gemma-4-e4b"));
        settings.setCheckRequestProfileId(QStringLiteral("teleocr"));
        settings.setModelRecipeId(QStringLiteral("unlimited-ocr"));
        LaunchProfileStore launchProfiles(settings);
        InstalledState installed(settings);
        RuntimeController runtime(settings, launchProfiles);
        ModelInstaller installer(settings, runtime, installed);

        QVERIFY(installer.setActiveModel(0, QStringLiteral("fix")).isEmpty());
        QCOMPARE(settings.fixRequestProfileId(), QStringLiteral("qwen3.5-4b"));

        // The same family answers the block recognition task too — LFM2.5-VL is
        // a general-purpose VL model — so the block selection follows its file.
        QVERIFY(installer.setActiveModel(1, QStringLiteral("blockRecognition")).isEmpty());
        QCOMPARE(settings.checkRequestProfileId(), QStringLiteral("lfm25-vl-3b"));

        // The qwen family does not answer the page OCR task, so the page
        // selection stays where it was rather than following the file.
        QVERIFY(installer.setActiveModel(0).isEmpty());
        QCOMPARE(settings.modelRecipeId(), QStringLiteral("unlimited-ocr"));

        QVERIFY(installer.setActiveModel(1).isEmpty());
        QCOMPARE(settings.modelRecipeId(), QStringLiteral("lfm25-vl-3b"));
    }

    // A model whose repo was renamed in the catalog kept the profile of the
    // model selected before it: the alias, the parser and the launch rows then
    // belonged to weights that were no longer loaded.
    void theStoredProfileFollowsTheActiveModel()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString modelsDir = QDir(root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("LiquidAI__LFM2.5-VL-3B-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        ModelEntry entry;
        entry.id = QStringLiteral("LiquidAI__LFM2.5-VL-3B-GGUF_Q8_0");
        entry.title = QStringLiteral("LFM2.5-VL-3B");
        entry.repo = QStringLiteral("LiquidAI/LFM2.5-VL-3B-GGUF");
        entry.dir = sub;
        entry.modelPath = writeGguf(sub, QStringLiteral("LFM2.5-VL-3B-Q8_0.gguf"));
        entry.mmprojPath = writeGguf(sub, QStringLiteral("mmproj-LFM2.5-VL-3B-F16.gguf"));
        entry.origin = ModelOrigin::Managed;
        entry.quantization = QStringLiteral("Q8_0");
        QVERIFY(!entry.modelPath.isEmpty());

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {entry}, err), qPrintable(err));

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        // The state a stale profile leaves behind: another model's id stored
        // next to this model's file.
        settings.setModelRecipeId(QStringLiteral("unlimited-ocr"));
        settings.setLaunchModelPath(entry.modelPath);
        LaunchProfileStore launchProfiles(settings);
        InstalledState installed(settings);
        RuntimeController runtime(settings, launchProfiles, &installed);

        ModelInstaller installer(settings, runtime, installed);
        QCOMPARE(settings.modelRecipeId(), QStringLiteral("lfm25-vl-3b"));
    }

    // A choice recorded while its quantization is still downloading (the task
    // window's Save) must not be reverted to the model at the launch path by
    // the stale-profile repair in refreshInstalled().
    void pendingDownloadChoiceKeepsTheRecordedProfile()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString modelsDir = QDir(root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("LiquidAI__LFM2.5-VL-3B-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        ModelEntry entry;
        entry.id = QStringLiteral("LiquidAI__LFM2.5-VL-3B-GGUF_Q8_0");
        entry.title = QStringLiteral("LFM2.5-VL-3B");
        entry.repo = QStringLiteral("LiquidAI/LFM2.5-VL-3B-GGUF");
        entry.dir = sub;
        entry.modelPath = writeGguf(sub, QStringLiteral("LFM2.5-VL-3B-Q8_0.gguf"));
        entry.mmprojPath = writeGguf(sub, QStringLiteral("mmproj-LFM2.5-VL-3B-F16.gguf"));
        entry.origin = ModelOrigin::Managed;
        entry.quantization = QStringLiteral("Q8_0");
        QVERIFY(!entry.modelPath.isEmpty());

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {entry}, err), qPrintable(err));

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        settings.setLaunchModelPath(entry.modelPath);
        LaunchProfileStore launchProfiles(settings);
        InstalledState installed(settings);
        RuntimeController runtime(settings, launchProfiles, &installed);
        ModelInstaller installer(settings, runtime, installed);
        QCOMPARE(settings.modelRecipeId(), QStringLiteral("lfm25-vl-3b"));

        // The task window's Save with a still-missing lightonocr quantization:
        // the pending final path takes over the launch setting at once.
        const QVariantList missing = installer.applyTaskChoices(
            {QVariantMap{{QStringLiteral("task"), QStringLiteral("pageOcr")}, {QStringLiteral("key"), QStringLiteral("lightonocr")}, {QStringLiteral("quantId"), QStringLiteral("Q8_0")}}});
        QCOMPARE(missing.size(), 1);
        QCOMPARE(settings.modelRecipeId(), QStringLiteral("lightonocr"));
        const QString pendingPath = settings.launchModelPath();
        QVERIFY(!pendingPath.isEmpty() && !QFileInfo::exists(pendingPath));

        // The refresh an reopened Models window runs must not read the old
        // model file as the user's choice.
        installer.refreshInstalled();
        QCOMPARE(settings.modelRecipeId(), QStringLiteral("lightonocr"));
        QCOMPARE(settings.launchModelPath(), pendingPath);
    }

    // One model+quant picked by several tasks is one download: the missing
    // list carries a single entry with every task's id merged in.
    void applyTaskChoicesMergeSharedDownloads()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        s->settings.setLaunchModelPath(QString());
        s->settings.setCheckLaunchModelPath(QString());
        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        QVariantList choices;
        choices.append(QVariantMap{{QStringLiteral("task"), QStringLiteral("pageOcr")}, {QStringLiteral("key"), QStringLiteral("lfm25-vl-3b")}, {QStringLiteral("quantId"), QStringLiteral("Q8_0")}});
        choices.append(
            QVariantMap{{QStringLiteral("task"), QStringLiteral("blockRecognition")}, {QStringLiteral("key"), QStringLiteral("lfm25-vl-3b")}, {QStringLiteral("quantId"), QStringLiteral("Q8_0")}});

        const QVariantList missing = s->installer->applyTaskChoices(choices);
        QCOMPARE(missing.size(), 1);
        const QVariantMap item = missing.first().toMap();
        QCOMPARE(item.value(QStringLiteral("task")).toString(), QStringLiteral("pageOcr"));
        QCOMPARE(item.value(QStringLiteral("tasks")).toList().size(), 2);
        // The profile intent is recorded for both tasks.
        QCOMPARE(s->settings.modelRecipeId(), QStringLiteral("lfm25-vl-3b"));
        QCOMPARE(s->settings.checkRequestProfileId(), QStringLiteral("lfm25-vl-3b"));
    }

    // The task window's Save (the rework plan's stage 6): an installed quant
    // is activated for its task, a missing one comes back for the download
    // dialog while the profile intent is recorded.
    void taskChoicesActivateInstalledAndListMissing()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        s->settings.setLaunchModelPath(QString());
        s->settings.setCheckLaunchModelPath(QString());

        // Two legacy (untagged) registry entries: the pageOcr-shaped one
        // carries a projector, the blockRecognition-shaped one does not —
        // matchesRole() splits the legacy entries by that heuristic.
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString ocrDir = QDir(modelsDir).filePath(QStringLiteral("org__repo"));
        QVERIFY(QDir().mkpath(ocrDir));
        ModelEntry q4;
        q4.id = QStringLiteral("org__repo_Q4_K_M");
        q4.title = QStringLiteral("org__repo");
        q4.repo = QStringLiteral("org/repo");
        q4.dir = ocrDir;
        q4.modelPath = writeGguf(ocrDir, QStringLiteral("model-Q4_K_M.gguf"));
        q4.mmprojPath = writeGguf(ocrDir, QStringLiteral("mmproj-model-F16.gguf"));
        q4.origin = ModelOrigin::Managed;
        q4.quantization = QStringLiteral("Q4_K_M");
        q4.byteSize = 1024;

        const QString blockDir = QDir(modelsDir).filePath(QStringLiteral("org__blockrepo"));
        QVERIFY(QDir().mkpath(blockDir));
        ModelEntry q8;
        q8.id = QStringLiteral("org__blockrepo_Q8_0");
        q8.title = QStringLiteral("org__blockrepo");
        q8.repo = QStringLiteral("org/blockrepo");
        q8.dir = blockDir;
        q8.modelPath = writeGguf(blockDir, QStringLiteral("model-Q8_0.gguf"));
        q8.origin = ModelOrigin::Managed;
        q8.quantization = QStringLiteral("Q8_0");
        q8.byteSize = 2048;

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {q4, q8}, err), qPrintable(err));
        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        auto *ocr = qobject_cast<ModelQuantModel *>(s->installer->quantModels());
        QVERIFY(ocr);
        QVERIFY(ocr->entryIndexFor(QStringLiteral("org__repo_Q4_K_M"), QStringLiteral("Q4_K_M")) >= 0);

        // The decision task has no installed entry here; its shipped profile
        // offers a downloadable quantization that is the missing one.
        auto *decision = qobject_cast<ModelQuantModel *>(s->installer->decisionQuantModels());
        QVERIFY(decision);
        const int clefRow = rowOfKey(decision, QStringLiteral("clef-flash"));
        QVERIFY(clefRow >= 0);
        QVariantMap missingPick;
        const QVariantList clefQuants = roleAt(decision, clefRow, ModelQuantModel::QuantsRole).toList();
        for (const QVariant &q : clefQuants) {
            const QVariantMap m = q.toMap();
            if (m.value(QStringLiteral("downloadable")).toBool() && !m.value(QStringLiteral("installed")).toBool()) {
                missingPick = m;
                break;
            }
        }
        QVERIFY(!missingPick.isEmpty());

        QVariantList choices;
        choices.append(
            QVariantMap{{QStringLiteral("task"), QStringLiteral("pageOcr")}, {QStringLiteral("key"), QStringLiteral("org__repo_Q4_K_M")}, {QStringLiteral("quantId"), QStringLiteral("Q4_K_M")}});
        choices.append(QVariantMap{
            {QStringLiteral("task"), QStringLiteral("blockRecognition")}, {QStringLiteral("key"), QStringLiteral("org__blockrepo_Q8_0")}, {QStringLiteral("quantId"), QStringLiteral("Q8_0")}});
        choices.append(QVariantMap{
            {QStringLiteral("task"), QStringLiteral("decision")}, {QStringLiteral("key"), QStringLiteral("clef-flash")}, {QStringLiteral("quantId"), missingPick.value(QStringLiteral("id"))}});

        const QVariantList missing = s->installer->applyTaskChoices(choices);
        QCOMPARE(s->settings.launchModelPath(), q4.modelPath);
        QCOMPARE(s->settings.checkLaunchModelPath(), q8.modelPath);
        QCOMPARE(missing.size(), 1);
        const QVariantMap item = missing.first().toMap();
        QCOMPARE(item.value(QStringLiteral("task")).toString(), QStringLiteral("decision"));
        QCOMPARE(item.value(QStringLiteral("key")).toString(), QStringLiteral("clef-flash"));
        QCOMPARE(item.value(QStringLiteral("quantId")).toString(), missingPick.value(QStringLiteral("id")).toString());
        // The profile intent survives a canceled queue.
        QCOMPARE(s->settings.selectedQuant(QStringLiteral("clef-flash")), missingPick.value(QStringLiteral("id")).toString());
    }

    // A queue item no row knows about fails fast and stops the queue — no
    // network is involved in this path.
    void taskQueueFailsFastOnUnknownQuant()
    {
        auto s = makeMultiQuantSetup();
        QVERIFY(s);
        QSignalSpy activeSpy(s->installer.data(), &ModelInstaller::taskQueueStateChanged);

        s->installer->queueTaskInstalls(
            {QVariantMap{{QStringLiteral("task"), QStringLiteral("pageOcr")}, {QStringLiteral("key"), QStringLiteral("no-such-row")}, {QStringLiteral("quantId"), QStringLiteral("Q4_K_M")}}});
        QCOMPARE(s->installer->stateInt(), (int)ModelInstaller::State::Error);
        QVERIFY(!s->installer->taskQueueActive());
        QCOMPARE(s->installer->taskQueueRemaining(), 0);
        QVERIFY(!s->installer->busy());

        // Cancel on an idle installer is a no-op that leaves no queue behind.
        s->installer->cancelTaskInstalls();
        QVERIFY(!s->installer->taskQueueActive());
    }

    // The management window's list (the rework plan's stage 7): one row per
    // profile across all the tasks plus standalone entries; an installed quant
    // of a profile's repo joins the profile row even when the task heuristics
    // would keep it out of that task's own list.
    void allModelsListEveryProfileAndEntry()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir = QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("sahilchachra__Unlimited-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        // An untagged mmproj-less quant inside a pageOcr profile's repo: the
        // pageOcr list drops it, the management list keeps it.
        ModelEntry chat;
        chat.id = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF_chat");
        chat.title = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF");
        chat.repo = QStringLiteral("sahilchachra/Unlimited-OCR-GGUF");
        chat.dir = sub;
        chat.modelPath = writeGguf(sub, QStringLiteral("Unlimited-OCR-chat-Q8_0.gguf"));
        chat.origin = ModelOrigin::Managed;
        chat.quantization = QStringLiteral("chat");

        ModelEntry stray;
        stray.id = QStringLiteral("stray_Q4_K_M");
        stray.title = QStringLiteral("stray");
        stray.repo = QStringLiteral("other/stray");
        stray.dir = QDir(modelsDir).filePath(QStringLiteral("other__stray"));
        QVERIFY(QDir().mkpath(stray.dir));
        stray.modelPath = writeGguf(stray.dir, QStringLiteral("stray-Q4_K_M.gguf"));
        stray.origin = ModelOrigin::Managed;
        stray.quantization = QStringLiteral("Q4_K_M");
        QVERIFY(!chat.modelPath.isEmpty() && !stray.modelPath.isEmpty());

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {chat, stray}, err), qPrintable(err));
        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        auto *all = qobject_cast<ModelQuantModel *>(s->installer->allModels());
        QVERIFY(all);

        // The shipped profiles are all here — including one that only a single
        // task list would show (clef-flash answers decision alone).
        QVERIFY(rowOfKey(all, QStringLiteral("clef-flash")) >= 0);

        const int row = rowOfKey(all, QStringLiteral("unlimited-ocr"));
        QVERIFY(row >= 0);
        QVERIFY(quantOf(all, row, QStringLiteral("CHAT")).value(QStringLiteral("installed")).toBool());
        auto *ocr = ocrModels(*s->installer);
        QCOMPARE(quantOf(ocr, rowOfKey(ocr, QStringLiteral("unlimited-ocr")), QStringLiteral("CHAT")).value(QStringLiteral("installed")).toBool(), false);

        // A repo no profile claims gets a row of its own, and its quantization
        // resolves back to the installer's registry index for the actions.
        const int strayRow = rowOfKey(all, QStringLiteral("stray_Q4_K_M"));
        QVERIFY(strayRow >= 0);
        QVERIFY(!roleAt(all, strayRow, ModelQuantModel::ProfileRole).toBool());
        QCOMPARE(all->entryIndexFor(QStringLiteral("stray_Q4_K_M"), QStringLiteral("Q4_K_M")), registryIndexOfQuant(*s->installer, QStringLiteral("Q4_K_M")));
    }

    void formatBytesRendersUnits()
    {
        QCOMPARE(formatBytes(0), QString());
        QCOMPARE(formatBytes(-5), QString());
        QCOMPARE(formatBytes(512 * 1024), QStringLiteral("512.0 KiB"));
        QCOMPARE(formatBytes(5 * 1024 * 1024), QStringLiteral("5.0 MiB"));
        QCOMPARE(formatBytes(4610580800LL), QStringLiteral("4.3 GiB"));
        QCOMPARE(formatBytes(1), QStringLiteral("0.0 KiB"));
    }

    // ADR 168: the task window's quant dropdown shows every profile
    // quantization with the curated download size; ADR 191 adds the card the
    // quantization needs. The file on disk wins once the quantization is
    // installed.
    void quantDropdownCarriesCuratedSizesAndTheCardRequirement()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        auto *check = checkModels(*s->installer);
        const int row = rowOfKey(check, QStringLiteral("teleocr"));
        QVERIFY(row >= 0);

        const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), QStringLiteral("teleocr"));
        QVERIFY(profile && profile->files.quants.size() >= 3);
        const qint64 curated = profile->files.quants.first().sizeBytes;
        QVERIFY(curated > 0);
        QVERIFY(profile->files.quants.first().id.compare(QStringLiteral("q8_0"), Qt::CaseInsensitive) == 0);

        QVariantMap quant = quantOf(check, row, QStringLiteral("Q8_0"));
        QVERIFY(!quant.isEmpty());
        QVERIFY(!quant.value(QStringLiteral("installed")).toBool());
        QCOMPARE(quant.value(QStringLiteral("size")).toLongLong(), curated);
        QCOMPARE(quant.value(QStringLiteral("sizeLabel")).toString(), formatBytes(curated));
        // the card the quantization needs: the weights, the projector, the KV
        // cache, the working set and the vision encoder.
        const qint64 vram = quant.value(QStringLiteral("vramBytes")).toLongLong();
        QVERIFY(vram > curated);
        QCOMPARE(quant.value(QStringLiteral("vramLabel")).toString(), formatBytes(vram));

        const QString sub = QDir(s->settings.runtimeModelsDir()).filePath(QStringLiteral("konradjr007__NaviDC-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));
        ModelEntry entry;
        entry.id = QStringLiteral("konradjr007__NaviDC-OCR-GGUF_Q8_0");
        entry.title = QStringLiteral("konradjr007__NaviDC-OCR-GGUF");
        entry.repo = QStringLiteral("konradjr007/NaviDC-OCR-GGUF");
        entry.dir = sub;
        entry.modelPath = writeGguf(sub, QStringLiteral("NaviDC-OCR-Q8_0.gguf"));
        entry.origin = ModelOrigin::Managed;
        entry.quantization = QStringLiteral("Q8_0");
        entry.byteSize = 12345678;
        entry.roles = {QStringLiteral("blockRecognition")};
        QVERIFY(!entry.modelPath.isEmpty());

        QString err;
        QVERIFY2(ModelRegistry::save(s->settings.runtimeModelsDir(), {entry}, err), qPrintable(err));
        s->installer->refreshInstalled();

        // Reconcile re-measures the size on disk; that measured size is what
        // the dropdown must report for the installed quantization.
        const ModelEntry *refreshed = nullptr;
        const QList<ModelEntry> entries = s->installer->installedEntries();
        for (const ModelEntry &candidate : entries) {
            if (candidate.quantization == QLatin1String("Q8_0"))
                refreshed = &candidate;
        }
        QVERIFY(refreshed && refreshed->byteSize != curated);
        quant = quantOf(check, row, QStringLiteral("Q8_0"));
        QVERIFY(quant.value(QStringLiteral("installed")).toBool());
        QCOMPARE(quant.value(QStringLiteral("size")).toLongLong(), refreshed->byteSize);
    }

    // The disk block of the Models window (ADR 168): one row per installed
    // entry with size and usage; the delete goes through the guards and keeps
    // files shared with sibling entries.
    void installedFilesListCarriesUsageAndDeletes()
    {
        std::unique_ptr<Setup> s = makeMultiQuantSetup();
        QVERIFY(s != nullptr);
        ModelInstaller &mi = *s->installer;

        const QString sub = QDir(s->settings.runtimeModelsDir()).filePath(QStringLiteral("org__repo"));
        const QString q4Path = QDir(sub).filePath(QStringLiteral("model-Q4_K_M.gguf"));
        const QString q8Path = QDir(sub).filePath(QStringLiteral("model-Q8_0.gguf"));
        const QString mmproj = QDir(sub).filePath(QStringLiteral("mmproj-model-F16.gguf"));

        auto *files = qobject_cast<InstalledFilesModel *>(mi.installedFiles());
        QVERIFY(files);
        QCOMPARE(files->rowCount(), 2);

        auto rowOfEntry = [files](const QString &entryId) {
            for (int i = 0; i < files->rowCount(); ++i) {
                if (files->data(files->index(i, 0), InstalledFilesModel::EntryIdRole).toString() == entryId)
                    return i;
            }
            return -1;
        };

        int row = rowOfEntry(QStringLiteral("org__repo_Q8_0"));
        QVERIFY(row >= 0);
        const qint64 measured = mi.installedEntries().at(registryIndexOfQuant(mi, QStringLiteral("Q8_0"))).byteSize;
        QCOMPARE(files->data(files->index(row, 0), InstalledFilesModel::UsageCountRole).toInt(), 0);
        QCOMPARE(files->data(files->index(row, 0), InstalledFilesModel::UsageTextRole).toString(), QStringLiteral("Not used"));
        QCOMPARE(files->data(files->index(row, 0), InstalledFilesModel::SizeLabelRole).toString(), formatBytes(measured));
        QVERIFY(files->data(files->index(row, 0), InstalledFilesModel::RemovableRole).toBool());

        const int q8 = registryIndexOfQuant(mi, QStringLiteral("Q8_0"));
        QVERIFY(q8 >= 0);
        QVERIFY(mi.setActiveModel(q8, QStringLiteral("pageOcr")).isEmpty());
        QVERIFY(mi.setActiveModel(q8, QStringLiteral("fix")).isEmpty());

        row = rowOfEntry(QStringLiteral("org__repo_Q8_0"));
        QVERIFY(row >= 0);
        QCOMPARE(files->data(files->index(row, 0), InstalledFilesModel::UsageCountRole).toInt(), 2);
        QCOMPARE(files->data(files->index(row, 0), InstalledFilesModel::UsageTextRole).toString(), QStringLiteral("Used by 2 task(s)"));

        QVERIFY(mi.removeInstalledEntry(QStringLiteral("org__repo_Q4_K_M")).isEmpty());
        QCOMPARE(files->rowCount(), 1);
        QVERIFY(!QFile::exists(q4Path));
        QVERIFY(QFile::exists(q8Path));
        QVERIFY(QFile::exists(mmproj));

        QVERIFY(!mi.removeInstalledEntry(QStringLiteral("no_such_entry")).isEmpty());
    }
};

QTEST_MAIN(TestModelInstaller)
#include "test_model_installer.moc"