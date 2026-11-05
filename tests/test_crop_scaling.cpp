#include <QImage>
#include <QPainter>
#include <QtTest>

#include "core/ModelProfiles.h"
#include "models/CropScaling.h"

using namespace llocr;

namespace {

// A "letter": a filled bar whose height the component pass measures exactly.
QImage barCrop(int width, int height, int letterHeight, int letters = 3)
{
    QImage crop(width, height, QImage::Format_RGB32);
    crop.fill(Qt::white);
    QPainter painter(&crop);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    const int spacing = width / (letters + 1);
    for (int i = 0; i < letters; ++i)
        painter.drawRect(spacing * (i + 1) - 3, (height - letterHeight) / 2, 6, letterHeight);
    painter.end();
    return crop;
}

}  // namespace

class TestCropScaling : public QObject
{
    Q_OBJECT

private slots:
    // The letter height is the median of the letter-like components: bars of
    // one height measure exactly, a rule or a blank measures as nothing.
    void measuresTheLetterHeight()
    {
        QCOMPARE(cropGlyphHeight(barCrop(60, 40, 16)), 16);
        QCOMPARE(cropGlyphHeight(barCrop(90, 40, 12, 5)), 12);
        QCOMPARE(cropGlyphHeight(QImage()), 0);

        QImage blank(60, 40, QImage::Format_RGB32);
        blank.fill(Qt::white);
        QCOMPARE(cropGlyphHeight(blank), 0);

        // A full-width bar is a rule, not a letter.
        QImage rule(60, 40, QImage::Format_RGB32);
        rule.fill(Qt::white);
        QPainter painter(&rule);
        painter.fillRect(0, 18, 60, 4, Qt::black);
        painter.end();
        QCOMPARE(cropGlyphHeight(rule), 0);
    }

    // The estimate mirrors the dynamic-resolution geometry; the calibrated
    // point is a dense table crop whose measured prompt was 1900 tokens.
    void estimatesVisionTokens()
    {
        QCOMPARE(estimateVisionTokens(QSize(28, 28)), 1);
        QCOMPARE(estimateVisionTokens(QSize(29, 29)), 2);
        QCOMPARE(estimateVisionTokens(QSize(2098, 696)), 1875);
    }

    // The glyph stage stretches only the labels in the map and only up.
    void glyphStageScalesMappedLabelsUpward()
    {
        CropScalingPolicy policy;
        policy.glyphHeightByBlock.insert(QStringLiteral("equation"), 32);

        const QImage crop = barCrop(60, 40, 16);
        const QImage scaled = applyCropScaling(crop, QStringLiteral("equation"), policy);
        QCOMPARE(scaled.size(), QSize(120, 80));
        QCOMPARE(cropGlyphHeight(scaled), 32);

        // A label outside the map passes untouched.
        const QImage untouched = applyCropScaling(crop, QStringLiteral("text"), policy);
        QCOMPARE(untouched.size(), crop.size());

        // Already at the target — no resample.
        const QImage big = barCrop(60, 40, 33);
        QCOMPARE(applyCropScaling(big, QStringLiteral("equation"), policy).size(), big.size());

        // The block task calls without a label: no glyph stage there.
        QCOMPARE(applyCropScaling(crop, QString(), policy).size(), crop.size());
    }

    // The budget stage shrinks only above the budget, down to the area the
    // token estimate saves.
    void budgetStageShrinksOverBudget()
    {
        CropScalingPolicy policy;
        policy.maxVisionTokens = 100;

        QImage crop(500, 500, QImage::Format_RGB32);
        crop.fill(Qt::white);
        QVERIFY(estimateVisionTokens(crop.size()) > 100);
        const QImage scaled = applyCropScaling(crop, QStringLiteral("text"), policy);
        QVERIFY(scaled.width() < crop.width());
        QVERIFY(estimateVisionTokens(scaled.size()) <= 100);

        QImage small(80, 80, QImage::Format_RGB32);
        small.fill(Qt::white);
        QVERIFY(estimateVisionTokens(small.size()) <= 100);
        QCOMPARE(applyCropScaling(small, QStringLiteral("text"), policy).size(), small.size());
    }

    // Both stages at once: the formula stretch happens first and the budget
    // still holds the answer; an empty policy is the identity.
    void stagesCompose()
    {
        CropScalingPolicy policy;
        policy.glyphHeightByBlock.insert(QStringLiteral("equation"), 32);
        policy.maxVisionTokens = 512;

        const QImage scaled = applyCropScaling(barCrop(600, 200, 12), QStringLiteral("equation"), policy);
        QVERIFY(scaled.width() > 600);
        QVERIFY(estimateVisionTokens(scaled.size()) <= 512);

        const QImage crop = barCrop(60, 40, 16);
        QCOMPARE(applyCropScaling(crop, QStringLiteral("equation"), CropScalingPolicy()).size(), crop.size());
    }

    // The profile role fills the policy; a missing role switches both stages
    // off.
    void policyComesFromTheRole()
    {
        ModelProfiles::Role role;
        role.cropMaxVisionTokens = 512;
        role.cropGlyphHeights.insert(QStringLiteral("equation"), 32);
        const CropScalingPolicy policy = policyForRole(&role);
        QCOMPARE(policy.maxVisionTokens, 512);
        QCOMPARE(policy.glyphHeightByBlock.value(QStringLiteral("equation")), 32);

        QVERIFY(policyForRole(nullptr).maxVisionTokens == 0);
        QVERIFY(policyForRole(nullptr).glyphHeightByBlock.isEmpty());
    }
};

QTEST_MAIN(TestCropScaling)

#include "test_crop_scaling.moc"
