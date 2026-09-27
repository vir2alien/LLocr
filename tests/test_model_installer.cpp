#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "app/LaunchProfileStore.h"
#include "app/SettingsStore.h"
#include "runtime/InstalledModelsModel.h"
#include "runtime/InstalledState.h"
#include "runtime/ModelInstaller.h"
#include "runtime/ModelRegistry.h"
#include "runtime/RuntimeController.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// Writes a placeholder GGUF file; returns its absolute path.
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
        const QString presetsPath =
            QDir(s.root.path()).filePath(QStringLiteral("launch-presets.json"));
        {
            QFile f(presetsPath);
            if (f.open(QIODevice::WriteOnly))
                f.write(QByteArrayLiteral(
                    "{ \"schemaVersion\": 1, \"profiles\": [] }").constData());
        }
        s.launchProfiles.reset(new LaunchProfileStore(s.settings, presetsPath));
        s.installed.reset(new InstalledState(s.settings));
        s.runtime.reset(new RuntimeController(s.settings, *s.launchProfiles, nullptr,
                                              s.installed.data()));
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
        const QString modelsDir =
            QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub = QDir(modelsDir).filePath(QStringLiteral("org__repo"));
        if (!QDir().mkpath(sub))
            return nullptr;

        const QString q4Path = writeGguf(sub, QStringLiteral("model-Q4_K_M.gguf"));
        const QString q8Path = writeGguf(sub, QStringLiteral("model-Q8_0.gguf"));
        const QString mmproj =
            writeGguf(sub, QStringLiteral("mmproj-model-F16.gguf"));
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
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime,
                                              *s->launchProfiles, *s->installed));
        return s;
    }

    // The list models replace the stringly-typed QVariantMap (ADR 115); the
    // helpers read a role through the same rows QML binds to.
    static QAbstractItemModel *ocrModels(ModelInstaller &installer)
    {
        return qobject_cast<QAbstractItemModel *>(installer.installedModels());
    }

    static QAbstractItemModel *checkModels(ModelInstaller &installer)
    {
        return qobject_cast<QAbstractItemModel *>(installer.checkInstalledModels());
    }

    static QString pathAt(QAbstractItemModel *model, int row)
    {
        return model->data(model->index(row, 0),
                           InstalledModelsModel::PathRole).toString();
    }

    // The *installer's* index, not the row: the list models are filtered per
    // role, and removeModel()/setActiveModel() address the registry list.
    static int indexOfQuant(ModelInstaller &installer, const QString &quant)
    {
        auto *model = qobject_cast<InstalledModelsModel *>(ocrModels(installer));
        for (int i = 0; i < model->rowCount(); ++i) {
            if (model->data(model->index(i, 0),
                            InstalledModelsModel::QuantizationRole).toString() == quant)
                return model->sourceIndex(i);
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
        const int idx = indexOfQuant(*s->installer, QStringLiteral("Q4_K_M"));
        QVERIFY(idx >= 0);

        const QString sub = QDir(s->settings.runtimeModelsDir())
                                .filePath(QStringLiteral("org__repo"));
        const QString q4Path =
            QDir(sub).filePath(QStringLiteral("model-Q4_K_M.gguf"));
        const QString q8Path =
            QDir(sub).filePath(QStringLiteral("model-Q8_0.gguf"));
        const QString mmproj =
            QDir(sub).filePath(QStringLiteral("mmproj-model-F16.gguf"));
        QVERIFY(QFile::exists(q4Path));
        QVERIFY(QFile::exists(q8Path));
        QVERIFY(QFile::exists(mmproj));

        QVERIFY(s->installer->removeModel(idx).isEmpty());
        QVERIFY(!QFile::exists(q4Path));  // the removed quant is gone
        QVERIFY(QFile::exists(q8Path));   // the other quant survives
        QVERIFY(QFile::exists(mmproj));   // shared mmproj survives (key assert)
        QVERIFY(QDir(sub).exists());      // shared folder survives

        // Registry now holds exactly the remaining quant.
        QString err;
        bool rebuilt = false;
        const QList<ModelEntry> entries =
            ModelRegistry::load(s->settings.runtimeModelsDir(), rebuilt, err);
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
        const int q4 = indexOfQuant(*s->installer, QStringLiteral("Q4_K_M"));
        QVERIFY(q4 >= 0);
        QVERIFY(s->installer->removeModel(q4).isEmpty());
        const int q8 = indexOfQuant(*s->installer, QStringLiteral("Q8_0"));
        QVERIFY(q8 >= 0);
        QVERIFY(s->installer->removeModel(q8).isEmpty());

        const QString sub = QDir(s->settings.runtimeModelsDir())
                                .filePath(QStringLiteral("org__repo"));
        QVERIFY(!QDir(sub).exists());  // the whole folder is gone
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
        QVERIFY(!q4Path.isEmpty() && !q8Path.isEmpty() && !mmprojA.isEmpty()
                && !mmprojB.isEmpty());

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
        const QString presetsPath =
            QDir(root.path()).filePath(QStringLiteral("launch-presets.json"));
        {
            QFile f(presetsPath);
            if (f.open(QIODevice::WriteOnly))
                f.write(QByteArrayLiteral(
                    "{ \"schemaVersion\": 1, \"profiles\": [] }").constData());
        }
        LaunchProfileStore launchProfiles(settings, presetsPath);
        RuntimeController runtime(settings, launchProfiles);
        InstalledState installed(settings);
        ModelInstaller installer(settings, runtime, launchProfiles, installed);
        const int idx = indexOfQuant(installer, q4.quantization);
        QVERIFY(idx >= 0);
        QVERIFY(installer.removeModel(idx).isEmpty());

        QVERIFY(!QFile::exists(q4Path));
        QVERIFY(!QFile::exists(mmprojA));  // unshared projector of removed quant
        QVERIFY(QFile::exists(q8Path));
        QVERIFY(QFile::exists(mmprojB));   // other quant's projector survives
    }

    // Preset Install buttons must be disabled for already-installed models.
    // Uses the built-in preset catalog: two presets of the same repo,
    // different quant files. Seeding one installed quant flips only its own
    // preset's flag, never the sibling's.
    void presetInstallFlagTracksInstalledModels()
    {
        auto s = std::make_unique<Setup>();
        pointAtTempDir(s->settings, s->root.path());
        const QString modelsDir =
            QDir(s->root.path()).filePath(QStringLiteral("models"));
        const QString sub =
            QDir(modelsDir).filePath(QStringLiteral("sahilchachra__Unlimited-OCR-GGUF"));
        QVERIFY(QDir().mkpath(sub));

        const QString q8Path =
            writeGguf(sub, QStringLiteral("Unlimited-OCR-Q8_0.gguf"));
        const QString mmproj =
            writeGguf(sub, QStringLiteral("mmproj-Unlimited-OCR-F16.gguf"));
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
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime,
                                              *s->launchProfiles, *s->installed));
        ModelInstaller &mi = *s->installer;

        auto findPreset = [&](const QString &presetId) {
            for (int i = 0; i < mi.presetCount(); ++i) {
                if (mi.presetInfo(i).value(QStringLiteral("id")).toString()
                    == presetId)
                    return i;
            }
            return -1;
        };
        const int q8Preset = findPreset(QStringLiteral("unlimited-ocr-q8_0"));
        const int q4Preset =
            findPreset(QStringLiteral("unlimited-ocr-q4_k_m.gguf"));
        QVERIFY(q8Preset >= 0);
        QVERIFY(q4Preset >= 0);

        // Q8_0 installed ⇒ its preset flag true; the sibling preset is not.
        QVERIFY(mi.presetInfo(q8Preset).value(QStringLiteral("installed")).toBool());
        QVERIFY(!mi.presetInfo(q4Preset).value(QStringLiteral("installed")).toBool());

        // Removing the installed quant clears its preset's flag.
        const int q8Idx = indexOfQuant(mi, QStringLiteral("Q8_0"));
        QVERIFY(q8Idx >= 0);
        QVERIFY(mi.removeModel(q8Idx).isEmpty());
        QVERIFY(!mi.presetInfo(q8Preset).value(QStringLiteral("installed")).toBool());
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
        QVERIFY(!ocrPath.isEmpty() && !mmproj.isEmpty() && !textPath.isEmpty()
                && !verifierPath.isEmpty());

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
        text.prompt = QStringLiteral("document parsing.");

        // A vision-capable model used as the verifier (legacy, no roles).
        ModelEntry verifier = ocr;
        verifier.id = QStringLiteral("org__repo_verify");
        verifier.modelPath = verifierPath;
        verifier.quantization = QStringLiteral("verify");

        QString err;
        QVERIFY2(ModelRegistry::save(modelsDir, {ocr, text, verifier}, err),
                 qPrintable(err));
        settings.setCheckLaunchModelPath(verifierPath);

        makeRuntime(*s);
        s->installer.reset(new ModelInstaller(s->settings, *s->runtime,
                                              *s->launchProfiles, *s->installed));
        ModelInstaller &mi = *s->installer;

        // OCR list: only the mmproj model — the verifier (active as the check
        // model) and the parser-carrying text model stay out of it.
        auto *ocrList = ocrModels(mi);
        auto *checkList = checkModels(mi);
        QVERIFY(ocrList);
        QVERIFY(checkList);
        QCOMPARE(ocrList->rowCount(), 1);
        QCOMPARE(pathAt(ocrList, 0), ocrPath);
        QCOMPARE(pathAt(ocrList, -1), QString());
        QCOMPARE(pathAt(ocrList, 5), QString());

        // Check list: the text model plus the active verifier.
        QCOMPARE(checkList->rowCount(), 2);
        QCOMPARE(pathAt(checkList, 0), textPath);
        QCOMPARE(pathAt(checkList, 1), verifierPath);

        // A row maps back to the installer's own list, for the calls the UI
        // drives by row (activate / remove / open folder).
        QCOMPARE(qobject_cast<InstalledModelsModel *>(ocrList)->sourceIndex(0), 0);
        QCOMPARE(qobject_cast<InstalledModelsModel *>(checkList)->sourceIndex(0), 1);
        QCOMPARE(qobject_cast<InstalledModelsModel *>(checkList)->sourceIndex(1), 2);

        // The role's active model is always listed: the text model becomes the
        // active OCR model and must appear in the OCR list too.
        settings.setLaunchModelPath(textPath);
        QCOMPARE(ocrList->rowCount(), 2);
        QCOMPARE(pathAt(ocrList, 1), textPath);
        QCOMPARE(checkList->rowCount(), 2);
    }

    // A list model with named roles: a QML delegate that misspells a role name
    // gets an empty cell at runtime instead of a compile error (ADR 115). The
    // rows must carry the values the delegate binds, and the model must not
    // reset when nothing changed.
    void installedModelsExposeNamedRoles()
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
        ModelInstaller installer(settings, runtime, launchProfiles, installed);

        auto *models = qobject_cast<InstalledModelsModel *>(installer.installedModels());
        QVERIFY(models);
        QCOMPARE(models->rowCount(), 1);
        QCOMPARE(models->data(models->index(0, 0), InstalledModelsModel::PathRole).toString(),
                 modelPath);
        QCOMPARE(models->data(models->index(0, 0), InstalledModelsModel::MmprojPathRole).toString(),
                 mmproj);
        QCOMPARE(models->data(models->index(0, 0), InstalledModelsModel::OriginRole).toString(),
                 QStringLiteral("managed"));
        QCOMPARE(models->data(models->index(0, 0), InstalledModelsModel::LicenseRole).toString(),
                 QString());
        QCOMPARE(models->data(models->index(0, 0), InstalledModelsModel::TitleRole).toString(),
                 QStringLiteral("ocr Q4_K_M"));
        // A row outside the model has no data, rather than reading row 0.
        QVERIFY(!models->data(models->index(7, 0), InstalledModelsModel::PathRole).isValid());

        // The roles the delegate binds are the ones the model declares.
        const QHash<int, QByteArray> names = models->roleNames();
        QCOMPARE(names.value(InstalledModelsModel::PathRole), QByteArray("path"));
        QCOMPARE(names.value(InstalledModelsModel::ActiveRole), QByteArray("active"));

        // A rescan that finds the same models must not reset the model.
        QSignalSpy resetSpy(models, &QAbstractItemModel::modelReset);
        installer.refreshInstalled();
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(models->rowCount(), 1);

        // Activating the model re-reads the highlight from the settings.
        QVERIFY(installer.setActiveModel(0).isEmpty());
        QCOMPARE(models->data(models->index(0, 0), InstalledModelsModel::ActiveRole).toBool(),
                 true);
    }

    // The highlight is *derived* from the settings, so activating a model
    // changes no entry and — when both models are visible in this role — not
    // even the row set. The view still has to be told, or the list keeps showing
    // the previous model as active until the window is rebuilt.
    void activatingAModelRepaintsTheHighlight()
    {
        std::unique_ptr<Setup> s = makeMultiQuantSetup();
        QVERIFY(s != nullptr);

        // Both selections set explicitly: the tests share one QSettings
        // process-wide, so an inherited value would decide the initial highlight.
        s->settings.setCheckLaunchModelPath(QString());
        s->settings.setLaunchModelPath(
            QDir(QDir(s->settings.runtimeModelsDir()).filePath(QStringLiteral("org__repo")))
                .filePath(QStringLiteral("model-Q4_K_M.gguf")));

        auto *models = qobject_cast<InstalledModelsModel *>(ocrModels(*s->installer));
        QVERIFY(models != nullptr);
        QCOMPARE(models->rowCount(), 2);
        QCOMPARE(models->data(models->index(0, 0), InstalledModelsModel::ActiveRole).toBool(),
                 true);
        QCOMPARE(models->data(models->index(1, 0), InstalledModelsModel::ActiveRole).toBool(),
                 false);

        QSignalSpy changed(models, &QAbstractItemModel::dataChanged);

        const int idx = indexOfQuant(*s->installer, QStringLiteral("Q8_0"));
        QVERIFY(idx >= 0);
        QVERIFY(s->installer->setActiveModel(idx).isEmpty());

        QCOMPARE(models->data(models->index(0, 0), InstalledModelsModel::ActiveRole).toBool(),
                 false);
        QCOMPARE(models->data(models->index(1, 0), InstalledModelsModel::ActiveRole).toBool(),
                 true);

        QVERIFY2(changed.count() > 0, "the list was not told to re-read the highlight");
        bool sawActiveRole = false;
        for (const QList<QVariant> &call : changed) {
            if (call.at(2).toList().contains(InstalledModelsModel::ActiveRole))
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
        ModelInstaller installer(settings, runtime, launchProfiles, installed);
        QVERIFY2(!installer.statusMessage().contains(QStringLiteral("no longer")),
                 qPrintable(installer.statusMessage()));

        // The user deletes the model directory.
        QVERIFY(QDir(sub).removeRecursively());
        installer.refreshInstalled();

        QCOMPARE(installer.installedCount(), 0);
        QVERIFY2(installer.statusMessage().contains(QStringLiteral("no longer")),
                 qPrintable(installer.statusMessage()));
        QVERIFY2(installer.statusMessage().contains(QStringLiteral("ocr-Q4_K_M.gguf")),
                 qPrintable(installer.statusMessage()));
    }
};

QTEST_MAIN(TestModelInstaller)
#include "test_model_installer.moc"