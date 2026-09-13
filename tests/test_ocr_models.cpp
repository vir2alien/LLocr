#include <QtTest>

#include "core/OcrRequest.h"
#include "models/OcrModelFactory.h"
#include "models/UnlimitedOcrModel.h"

using namespace llocr;

class TestOcrModels : public QObject {
    Q_OBJECT

private slots:
    void factoryDefaultAndRegistry() {
        QCOMPARE(OcrModelFactory::defaultId(), QStringLiteral("unlimited-ocr"));
        QVERIFY(OcrModelFactory::registeredIds().contains(QStringLiteral("unlimited-ocr")));

        const auto model = OcrModelFactory::create(OcrModelFactory::defaultId());
        QVERIFY(model != nullptr);
        QCOMPARE(model->id(), QStringLiteral("unlimited-ocr"));
    }

    void unknownIdFallsBackToDefault() {
        const auto model = OcrModelFactory::create(QStringLiteral("no-such-model"));
        QVERIFY(model != nullptr);
        QCOMPARE(model->id(), OcrModelFactory::defaultId());
    }

    void idNameMapping() {
        const QString id = OcrModelFactory::defaultId();
        const QString name = OcrModelFactory::displayNameForId(id);
        QVERIFY(!name.isEmpty());
        QCOMPARE(OcrModelFactory::idForDisplayName(name), id);
    }

    void unlimitedModelContract() {
        UnlimitedOcrModel model;

        QCOMPARE(model.id(), QStringLiteral("unlimited-ocr"));
        QCOMPARE(model.displayName(), QStringLiteral("Unlimited-OCR"));
        QCOMPARE(model.defaultParserId(), QStringLiteral("det_tokens"));

        const QList<OcrPromptVariant> variants = model.promptVariants();
        QCOMPARE(variants.size(), 1);
        QCOMPARE(variants.first().text, QStringLiteral("document parsing."));
        QVERIFY(!variants.first().id.isEmpty());
        QVERIFY(!variants.first().title.isEmpty());
    }
};

QTEST_MAIN(TestOcrModels)
#include "test_ocr_models.moc"
