#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "app/SettingsStore.h"
#include "runtime/ModelInstaller.h"
#include "runtime/ModelRegistry.h"
#include "runtime/RuntimeController.h"

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
    struct Setup {
        QTemporaryDir root;
        SettingsStore settings;
        RuntimeController runtime{settings};
        QScopedPointer<ModelInstaller> installer;
    };

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

        s->installer.reset(new ModelInstaller(s->settings, s->runtime));
        return s;
    }

    static int indexOfQuant(ModelInstaller &installer, const QString &quant)
    {
        for (int i = 0; i < installer.installedCount(); ++i) {
            if (installer.installedInfo(i)
                    .value(QStringLiteral("quantization"))
                    .toString() == quant)
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

        RuntimeController runtime(settings);
        ModelInstaller installer(settings, runtime);
        const int idx = indexOfQuant(installer, q4.quantization);
        QVERIFY(idx >= 0);
        QVERIFY(installer.removeModel(idx).isEmpty());

        QVERIFY(!QFile::exists(q4Path));
        QVERIFY(!QFile::exists(mmprojA));  // unshared projector of removed quant
        QVERIFY(QFile::exists(q8Path));
        QVERIFY(QFile::exists(mmprojB));   // other quant's projector survives
    }
};

QTEST_MAIN(TestModelInstaller)
#include "test_model_installer.moc"