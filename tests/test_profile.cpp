#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "core/ModelProfile.h"

using namespace llocr;

class TestProfile : public QObject {
    Q_OBJECT

private slots:
    // A complete profile parses all fields correctly.
    void parsesFullProfile() {
        const QByteArray json = R"({
            "model_id": "baidu/Unlimited-OCR",
            "display_name": "Unlimited-OCR",
            "prompt_template": "document parsing.",
            "output_parser": "det_tokens",
            "supports_bbox": true,
            "bbox_coordinate_range": 1000,
            "generation": { "temperature": 0.0, "max_tokens": 32768 },
            "image": { "preferred_format": "png" }
        })";

        const QJsonObject obj = QJsonDocument::fromJson(json).object();
        bool ok = false;
        const ModelProfile p = ModelProfile::fromJson(obj, &ok);

        QVERIFY(ok);
        QVERIFY(p.isValid());
        QCOMPARE(p.modelId, QStringLiteral("baidu/Unlimited-OCR"));
        QCOMPARE(p.promptTemplate, QStringLiteral("document parsing."));
        QCOMPARE(p.outputParser, QStringLiteral("det_tokens"));
        QVERIFY(p.supportsBbox);
        QCOMPARE(p.bboxCoordinateRange, 1000);
        QCOMPARE(p.generation.maxTokens, 32768);
        QVERIFY(qFuzzyIsNull(p.generation.temperature));
    }

    // Missing model_id makes the profile invalid.
    void rejectsMissingModelId() {
        const QByteArray json = R"({ "display_name": "No id here" })";
        const QJsonObject obj = QJsonDocument::fromJson(json).object();

        bool ok = true;
        const ModelProfile p = ModelProfile::fromJson(obj, &ok);

        QVERIFY(!ok);
        QVERIFY(!p.isValid());
    }

    // Optional fields fall back to defaults.
    void appliesDefaults() {
        const QByteArray json = R"({ "model_id": "some/model" })";
        const QJsonObject obj = QJsonDocument::fromJson(json).object();

        bool ok = false;
        const ModelProfile p = ModelProfile::fromJson(obj, &ok);

        QVERIFY(ok);
        QCOMPARE(p.outputParser, QStringLiteral("raw"));       // default parser
        QCOMPARE(p.bboxCoordinateRange, 1000);                 // default range
        QCOMPARE(p.generation.maxTokens, 4096);                // default tokens
        QCOMPARE(p.preferredImageFormat, QStringLiteral("png")); // default format
        // display_name falls back to model_id.
        QCOMPARE(p.displayName, QStringLiteral("some/model"));
    }
};

QTEST_MAIN(TestProfile)
#include "test_profile.moc"
