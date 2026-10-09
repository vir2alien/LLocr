#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "config/SettingsStore.h"
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
    static QString pathOf(ModelInstaller &installer, const QString &key, const QString &role = QStringLiteral("ocr"))
    {
        auto *model = qobject_cast<ModelQuantModel *>(role == QLatin1String("blockRecognition") ? installer.checkQuantModels()
                                                      : role == QLatin1String("decision")       ? installer.decisionQuantModels()
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

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_model_installer"));
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
        QCOMPARE(quants.size(), 2);
        QCOMPARE(quants.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("Q8_0"));
        QCOMPARE(quants.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("Q4_K_M"));
        QVERIFY(quants.at(0).toMap().value(QStringLiteral("installed")).toBool());
        QVERIFY(!quants.at(1).toMap().value(QStringLiteral("installed")).toBool());
        // Both are downloadable: the profile pins a file for each.
        QVERIFY(quants.at(0).toMap().value(QStringLiteral("downloadable")).toBool());
        QVERIFY(quants.at(1).toMap().value(QStringLiteral("downloadable")).toBool());

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

        QStringList ocrKeys;
        for (int i = 0; i < ocr->rowCount(); ++i)
            ocrKeys.append(roleAt(ocr, i, ModelQuantModel::KeyRole).toString());
        QCOMPARE(ocrKeys, QStringList({QStringLiteral("unlimited-ocr"), QStringLiteral("lfm25-vl-3b"), QStringLiteral("teleocr")}));

        QStringList checkKeys;
        for (int i = 0; i < check->rowCount(); ++i)
            checkKeys.append(roleAt(check, i, ModelQuantModel::KeyRole).toString());
        QCOMPARE(checkKeys, QStringList({QStringLiteral("lfm25-vl-3b"), QStringLiteral("qwen3.5-4b"), QStringLiteral("qwen3.5-9b"), QStringLiteral("teleocr")}));

        // The decision list serves the decision role only.
        QStringList decisionKeys;
        for (int i = 0; i < decision->rowCount(); ++i)
            decisionKeys.append(roleAt(decision, i, ModelQuantModel::KeyRole).toString());
        QCOMPARE(decisionKeys, QStringList({QStringLiteral("d1-3b")}));
        QVERIFY(pathOf(*s->installer, QStringLiteral("d1-3b"), QStringLiteral("decision")).isEmpty());

        // TeleOCR answers both roles and offers the quantizations its profile
        // declares — the shipped file is the source here, not a copy of it, so an
        // edit to the catalog is what this follows.
        const int teleocr = rowOfKey(ocr, QStringLiteral("teleocr"));
        QVERIFY(teleocr >= 0);
        const ModelProfiles::Profile *teleocrProfile = ModelProfiles::find(ModelProfiles::instance(), QStringLiteral("teleocr"));
        QVERIFY(teleocrProfile);
        const QVariantList quants = roleAt(ocr, teleocr, ModelQuantModel::QuantsRole).toList();
        QCOMPARE(quants.size(), teleocrProfile->files.quants.size());
        QCOMPARE(quants.first().toMap().value(QStringLiteral("id")).toString(), teleocrProfile->files.quants.first().id.toUpper());
        QVERIFY(!roleAt(ocr, teleocr, ModelQuantModel::SelectedInstalledRole).toBool());
        QVERIFY(roleAt(ocr, teleocr, ModelQuantModel::SelectedDownloadableRole).toBool());
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

        const QString oddPath = writeGguf(sub, QStringLiteral("Unlimited-OCR-Q6_K.gguf"));
        QVERIFY(!oddPath.isEmpty());

        ModelEntry odd;
        odd.id = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF_Q6_K");
        odd.title = QStringLiteral("sahilchachra__Unlimited-OCR-GGUF");
        odd.repo = QStringLiteral("sahilchachra/Unlimited-OCR-GGUF");
        odd.dir = sub;
        odd.modelPath = oddPath;
        odd.origin = ModelOrigin::Managed;
        odd.quantization = QStringLiteral("Q6_K");
        odd.roles = {QStringLiteral("ocr")};

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {odd}, err), qPrintable(err));

        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime, *s->installed));

        auto *models = ocrModels(*s->installer);
        const int row = rowOfKey(models, QStringLiteral("unlimited-ocr"));
        QVERIFY(row >= 0);
        const QVariantMap extra = quantOf(models, row, QStringLiteral("Q6_K"));
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
        external.roles = {QStringLiteral("ocr")};
        QVERIFY2(ModelRegistry::save(modelsDir, {odd, external}, err), qPrintable(err));

        s->installer->refreshInstalled();
        const int externalRow = rowOfKey(models, QStringLiteral("outside_model"));
        QVERIFY(externalRow >= 0);
        QVERIFY(!roleAt(models, externalRow, ModelQuantModel::ProfileRole).toBool());
        QCOMPARE(roleAt(models, externalRow, ModelQuantModel::QuantsRole).toList().size(), 1);
        QCOMPARE(roleAt(models, externalRow, ModelQuantModel::SubtitleRole).toString(), external.dir);
        QCOMPARE(pathOf(*s->installer, QStringLiteral("outside_model")), externalPath);
    }

    // The selected quantization is a choice the user makes and it has to survive
    // closing the window; the active model wins over it only when the user has not
    // picked anything.
    void selectedQuantIsRememberedAndYieldsToTheActiveModel()
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
        entry.roles = {QStringLiteral("ocr")};

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {entry}, err), qPrintable(err));
        mi.refreshInstalled();

        // With a quantization on disk and nothing chosen, that one is shown.
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q4_K_M"));

        mi.selectQuant(QStringLiteral("unlimited-ocr"), QStringLiteral("Q8_0"));
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q8_0"));
        QCOMPARE(s->settings.selectedQuant(QStringLiteral("unlimited-ocr")), QStringLiteral("Q8_0"));

        // Activating the installed quantization does not move the picker: the user
        // is comparing them, and the row keeps showing what was picked.
        QVERIFY(mi.useQuant(QStringLiteral("unlimited-ocr"), QStringLiteral("Q4_K_M")).isEmpty());
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedActiveRole).toBool(), false);
        QVERIFY(roleAt(models, row, ModelQuantModel::ActiveRole).toBool());
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedInstalledRole).toBool(), false);

        // A pick that is not in the profile is refused rather than silently applied.
        mi.selectQuant(QStringLiteral("unlimited-ocr"), QStringLiteral("Q2_K"));
        QCOMPARE(roleAt(models, row, ModelQuantModel::SelectedLabelRole).toString(), QStringLiteral("Q8_0"));

        s->settings.setSelectedQuant(QStringLiteral("unlimited-ocr"), QString());
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
        q8.roles = {QStringLiteral("ocr")};

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
        QCOMPARE(rowOfKey(ocrList, QStringLiteral("org__repo_ocr")), 3);
        QCOMPARE(rowOfKey(ocrList, QStringLiteral("org__repo_chat")), -1);
        QCOMPARE(rowOfKey(ocrList, QStringLiteral("org__repo_verify")), -1);
        QCOMPARE(rowOfKey(checkList, QStringLiteral("org__repo_chat")), 4);
        QCOMPARE(rowOfKey(checkList, QStringLiteral("org__repo_verify")), 5);
        QCOMPARE(pathOf(mi, QStringLiteral("org__repo_ocr")), ocrPath);
        QCOMPARE(pathOf(mi, QStringLiteral("org__repo_chat"), QStringLiteral("blockRecognition")), textPath);
        QCOMPARE(pathOf(mi, QStringLiteral("org__repo_verify"), QStringLiteral("blockRecognition")), verifierPath);

        // A quantization of a model the user brought resolves back to the
        // installer's own index, which is what activate / delete act on.
        QCOMPARE(quantModel(ocrList)->entryIndexFor(QStringLiteral("org__repo_ocr"), QStringLiteral("Q4_K_M")), 0);

        // The role's active model is always listed: the text model becomes the
        // active OCR model and must appear in the OCR list too.
        settings.setLaunchModelPath(textPath);
        QCOMPARE(rowOfKey(ocrList, QStringLiteral("org__repo_chat")), 4);
        QCOMPARE(rowOfKey(checkList, QStringLiteral("org__repo_chat")), 4);
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
        // The models of the role, and the one the user brought themselves.
        QCOMPARE(models->rowCount(), 4);

        const QHash<int, QByteArray> names = models->roleNames();
        QCOMPARE(names.value(ModelQuantModel::KeyRole), QByteArray("key"));
        QCOMPARE(names.value(ModelQuantModel::ActiveRole), QByteArray("active"));
        QCOMPARE(names.value(ModelQuantModel::SelectedLabelRole), QByteArray("selectedLabel"));

        const int row = rowOfKey(models, QStringLiteral("org__repo"));
        QVERIFY(row >= 0);
        QVERIFY(!roleAt(models, row, ModelQuantModel::ProfileRole).toBool());
        QCOMPARE(roleAt(models, row, ModelQuantModel::TitleRole).toString(), QStringLiteral("ocr Q4_K_M"));
        QCOMPARE(roleAt(models, row, ModelQuantModel::SubtitleRole).toString(), sub);
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
        QCOMPARE(models->rowCount(), 4);

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

        QVERIFY(s->installer->useQuant(QStringLiteral("org__repo_Q8_0"), QStringLiteral("Q8_0")).isEmpty());

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
        settings.setCheckRequestProfileId(QStringLiteral("qwen3.5-4b"));
        settings.setModelRecipeId(QStringLiteral("unlimited-ocr"));
        LaunchProfileStore launchProfiles(settings);
        InstalledState installed(settings);
        RuntimeController runtime(settings, launchProfiles);
        ModelInstaller installer(settings, runtime, installed);

        QVERIFY(installer.setActiveModel(0, QStringLiteral("blockRecognition")).isEmpty());
        QCOMPARE(settings.checkRequestProfileId(), QStringLiteral("qwen3.5-4b"));

        // The same family does not answer the ocr role, so the ocr selection
        // stays where it was rather than following the file.
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
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("konradjr007__NaviDC-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        ModelEntry entry;
        entry.id = QStringLiteral("konradjr007__NaviDC-OCR-GGUF_Q8_0");
        entry.title = QStringLiteral("NaviDC");
        entry.repo = QStringLiteral("konradjr007/NaviDC-OCR-GGUF");
        entry.dir = sub;
        entry.modelPath = writeGguf(sub, QStringLiteral("NaviDC-OCR-Q8_0.gguf"));
        entry.mmprojPath = writeGguf(sub, QStringLiteral("NaviDC-OCR-mmproj-q8_0.gguf"));
        entry.origin = ModelOrigin::Managed;
        entry.quantization = QStringLiteral("Q8_0");
        QVERIFY(!entry.modelPath.isEmpty());

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {entry}, err), qPrintable(err));

        SettingsStore settings;
        pointAtTempDir(settings, root.path());
        // The state a renamed profile leaves behind: another model's id stored
        // next to this model's file.
        settings.setModelRecipeId(QStringLiteral("unlimited-ocr"));
        settings.setLaunchModelPath(entry.modelPath);
        LaunchProfileStore launchProfiles(settings);
        InstalledState installed(settings);
        RuntimeController runtime(settings, launchProfiles, &installed);

        ModelInstaller installer(settings, runtime, installed);
        QCOMPARE(settings.modelRecipeId(), QStringLiteral("teleocr"));
    }
};

QTEST_MAIN(TestModelInstaller)
#include "test_model_installer.moc"