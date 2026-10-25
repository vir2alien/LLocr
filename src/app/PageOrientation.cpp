#include "app/PageOrientation.h"

#include <algorithm>
#include <cmath>

#include <QDebug>
#include <QVector>

namespace llocr {
namespace {

// The check works on a small copy: the lines only have to be told apart, and
// a page-sized picture would cost more than the answer is worth.
constexpr int kWorkingEdge = 1200;

// A page that is nearly empty or nearly black carries no readable text.
constexpr double kMinInkFraction = 0.004;
constexpr double kMaxInkFraction = 0.45;

// A row or a column counts as carrying ink above this share of the profile's
// peak. The share is low enough to keep a line in one piece across the gaps
// between its words, and high enough to tell the lines apart even where the
// leading is tight.
constexpr double kBandRatio = 0.45;

// A peak below this share of the strongest one is noise, not a line.
constexpr double kPeakRatio = 0.2;

// The signed gap score this far from zero is a decision.
constexpr double kGapMargin = 0.08;

// The gaps have to carry this much ink, against the peak of the profile, for
// the reading to mean anything.
constexpr double kMinGapInk = 0.05;

// The winning direction has to be this much clearer than the other one: a
// page that reads both ways (a table, a form) is left to the model.
constexpr double kDirectionRatio = 2.0;

// Fewer lines than this and the page says nothing about its orientation.
constexpr int kMinLines = 4;

// The threshold between the ink and the paper, chosen the way Otsu does it:
// the split with the widest gap between the two classes.
int otsuThreshold(const QImage &gray)
{
    int histogram[256] = {};
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            ++histogram[line[x]];
    }

    const double total = double(gray.width()) * double(gray.height());
    double sum = 0.0;
    for (int i = 0; i < 256; ++i)
        sum += i * double(histogram[i]);

    double sumBackground = 0.0;
    double weightBackground = 0.0;
    double bestVariance = -1.0;
    int threshold = 128;
    for (int t = 0; t < 256; ++t) {
        weightBackground += histogram[t];
        if (weightBackground <= 0.0)
            continue;
        const double weightForeground = total - weightBackground;
        if (weightForeground <= 0.0)
            break;
        sumBackground += t * double(histogram[t]);
        const double meanBackground = sumBackground / weightBackground;
        const double meanForeground = (sum - sumBackground) / weightForeground;
        const double difference = meanBackground - meanForeground;
        const double variance = weightBackground * weightForeground * difference * difference;
        if (variance > bestVariance) {
            bestVariance = variance;
            threshold = t;
        }
    }
    return threshold;
}

// A band of the profile that carries the body of a line. The band is where
// the letters stand; above and below it the ascenders and the descenders
// reach out, and those are what tell the two ways up apart.
struct CoreBand {
    int top = 0;
    int bottom = 0;
    int peak = 0;
    int peakValue = 0;
};

// The signed answer of the gap reading: positive when the text is upright.
struct GapScore {
    double score = 0.0;       // -1..1 — the ink below the gap against the ink above it.
    double weight = 0.0;      // The ink the gaps carry, against the peak of the profile.
    bool meaningful = false;  // The gaps carry enough ink for the reading to mean anything.
};

// The strongest value of a profile.
int profilePeak(const QVector<int> &profile)
{
    int peak = 0;
    for (const int value : profile)
        peak = qMax(peak, value);
    return peak;
}

// The bands of the profile that carry the lines of text.
QList<CoreBand> findCoreBands(const QVector<int> &profile)
{
    QList<CoreBand> bands;
    if (profile.size() < 8)
        return bands;

    // The profile of the lines, smoothed: a stroke is not a line.
    QVector<int> smooth(profile.size());
    for (int i = 0; i < profile.size(); ++i) {
        const int left = profile[qMax(0, i - 1)];
        const int right = profile[qMin(profile.size() - 1, i + 1)];
        smooth[i] = (left + 2 * profile[i] + right) / 4;
    }

    int strongest = 0;
    for (const int value : smooth)
        strongest = qMax(strongest, value);
    if (strongest <= 0)
        return bands;

    QList<int> peaks;
    for (int i = 0; i < smooth.size(); ++i) {
        const int left = i > 0 ? smooth[i - 1] : 0;
        const int right = i + 1 < smooth.size() ? smooth[i + 1] : 0;
        if (smooth[i] < left || smooth[i] <= right)
            continue;
        if (double(smooth[i]) < strongest * kPeakRatio)
            continue;
        // Two peaks close together belong to the same line: keep the higher.
        if (!peaks.isEmpty() && i - peaks.last() <= 2) {
            if (smooth[i] > smooth[peaks.last()])
                peaks.last() = i;
            continue;
        }
        peaks.append(i);
    }

    for (const int peak : peaks) {
        const int limit = qMax(1, int(smooth[peak] * kBandRatio));
        int top = peak;
        while (top > 0 && smooth[top - 1] >= limit)
            --top;
        int bottom = peak;
        while (bottom + 1 < smooth.size() && smooth[bottom + 1] >= limit)
            ++bottom;
        bands.append({top, bottom, peak, smooth[peak]});
    }
    return bands;
}

// The space between two neighbouring lines carries the descenders of the
// upper one and the ascenders of the lower one. The alphabets have more
// ascenders than descenders, so upright text leaves more ink in the lower
// half of the gap than in the upper one.
GapScore scoreGaps(const QVector<int> &profile, const QList<CoreBand> &bands, int peak)
{
    GapScore result;
    if (bands.size() < kMinLines || peak <= 0)
        return result;

    qint64 lower = 0;
    qint64 upper = 0;
    int gaps = 0;
    for (int i = 0; i + 1 < bands.size(); ++i) {
        const int gapStart = bands[i].bottom + 1;
        const int gapEnd = bands[i + 1].top - 1;
        const int height = gapEnd - gapStart + 1;
        if (height < 3)
            continue;
        const int middle = gapStart + height / 2;
        for (int y = gapStart; y < middle; ++y)
            upper += profile[y];
        for (int y = middle; y <= gapEnd; ++y)
            lower += profile[y];
        ++gaps;
    }

    const qint64 total = lower + upper;
    if (total <= 0 || gaps == 0)
        return result;
    result.score = double(lower - upper) / double(total);
    // The gaps of a page of text carry the ascenders and the descenders; the
    // gaps of the other direction carry nothing at all.
    result.weight = double(total) / (double(gaps) * double(peak));
    result.meaningful = result.weight >= kMinGapInk;
    return result;
}

class PageScanner
{
public:
    explicit PageScanner(const QImage &image);

    OrientationVerdict verdict();

private:
    void binarize();
    void buildProfiles();

    QImage m_gray;
    QVector<uchar> m_ink;  // One byte per pixel: 1 is ink.
    int m_width = 0;
    int m_height = 0;
    int m_inkCount = 0;
    QVector<int> m_rowInk;
    QVector<int> m_colInk;
};

PageScanner::PageScanner(const QImage &image)
{
    QImage scaled = image;
    const int longest = qMax(scaled.width(), scaled.height());
    if (longest > kWorkingEdge) {
        const QSize fit = scaled.size().scaled(QSize(kWorkingEdge, kWorkingEdge), Qt::KeepAspectRatio);
        scaled = scaled.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    m_gray = scaled.convertToFormat(QImage::Format_Grayscale8);
    m_width = m_gray.width();
    m_height = m_gray.height();
    binarize();
    buildProfiles();
}

void PageScanner::binarize()
{
    const int threshold = otsuThreshold(m_gray);
    m_ink.assign(m_width * m_height, 0);
    for (int y = 0; y < m_height; ++y) {
        const uchar *line = m_gray.constScanLine(y);
        uchar *ink = m_ink.data() + y * m_width;
        for (int x = 0; x < m_width; ++x) {
            if (line[x] <= threshold) {
                ink[x] = 1;
                ++m_inkCount;
            }
        }
    }
}

void PageScanner::buildProfiles()
{
    m_rowInk.assign(m_height, 0);
    m_colInk.assign(m_width, 0);
    for (int y = 0; y < m_height; ++y) {
        const uchar *ink = m_ink.constData() + y * m_width;
        int rowTotal = 0;
        for (int x = 0; x < m_width; ++x) {
            if (!ink[x])
                continue;
            ++rowTotal;
            ++m_colInk[x];
        }
        m_rowInk[y] = rowTotal;
    }
}

OrientationVerdict PageScanner::verdict()
{
    OrientationVerdict result;

    if (m_width <= 0 || m_height <= 0)
        return result;
    const double inkFraction = double(m_inkCount) / double(m_width * m_height);
    if (inkFraction < kMinInkFraction || inkFraction > kMaxInkFraction)
        return result;
    result.textLike = true;

    const QList<CoreBand> rowBands = findCoreBands(m_rowInk);
    const QList<CoreBand> colBands = findCoreBands(m_colInk);

    // The reading is done in both directions: the direction the text runs in
    // is the one whose gaps carry the ascenders and the descenders, and the
    // other direction's gaps carry nothing at all.
    const GapScore horizontal = scoreGaps(m_rowInk, rowBands, profilePeak(m_rowInk));
    const GapScore vertical = scoreGaps(m_colInk, colBands, profilePeak(m_colInk));

    const bool horizontalWins = horizontal.meaningful && (!vertical.meaningful || qAbs(horizontal.score) >= kDirectionRatio * qAbs(vertical.score));
    const bool verticalWins = vertical.meaningful && (!horizontal.meaningful || qAbs(vertical.score) >= kDirectionRatio * qAbs(horizontal.score));
    if (horizontalWins == verticalWins)
        return result;

    const GapScore &chosen = horizontalWins ? horizontal : vertical;
    if (!chosen.meaningful || qAbs(chosen.score) < kGapMargin)
        return result;

    result.decided = true;
    result.confidence = qAbs(chosen.score);
    if (horizontalWins)
        result.degrees = chosen.score > 0.0 ? 0 : 180;
    else
        result.degrees = chosen.score > 0.0 ? 90 : 270;
    return result;
}

}  // namespace

OrientationVerdict detectPageOrientation(const QImage &image)
{
    if (image.isNull())
        return {};
    PageScanner scanner(image);
    return scanner.verdict();
}

}  // namespace llocr
