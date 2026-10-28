#include "app/PageOrientation.h"

#include <QVector>

namespace llocr {
namespace {

// The check works on a small copy: the lines only have to be told apart, and
// a page-sized picture would cost more than the answer is worth.
constexpr int kWorkingEdge = 1200;

// A page that is nearly empty or nearly black carries no readable text.
constexpr double kMinInkFraction = 0.004;
constexpr double kMaxInkFraction = 0.45;

// The axis of the text has to be this much clearer than the other one: a
// page that reads both ways (a table, a form) is left undecided.
constexpr double kAxisMargin = 1.5;

// Fewer lines than this and the page carries no text to speak of: a rule, a
// divider, a single line of a caption.
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

// How much the profile of a direction varies. The profile of the lines of the
// text alternates — a line, a gap, a line — so its variance is large; across
// the text the ink runs smooth and the variance stays small.
double profileVariance(const QVector<int> &profile)
{
    if (profile.isEmpty())
        return 0.0;
    double mean = 0.0;
    for (const int value : profile)
        mean += value;
    mean /= profile.size();
    double sum = 0.0;
    for (const int value : profile) {
        const double difference = value - mean;
        sum += difference * difference;
    }
    return sum / profile.size();
}

// The number of separate lines of ink in a profile: the stretches that carry
// more than a fifth of the profile's peak.
int countLines(const QVector<int> &profile)
{
    int peak = 0;
    for (const int value : profile)
        peak = qMax(peak, value);
    if (peak <= 0)
        return 0;
    const int limit = qMax(1, int(peak * 0.2));
    int lines = 0;
    bool inside = false;
    for (const int value : profile) {
        const bool carries = value >= limit;
        if (carries && !inside) {
            ++lines;
            inside = true;
        } else if (!carries) {
            inside = false;
        }
    }
    return lines;
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

    const double rowVariance = profileVariance(m_rowInk);
    const double colVariance = profileVariance(m_colInk);
    const double stronger = qMax(rowVariance, colVariance);
    const double weaker = qMin(rowVariance, colVariance);
    if (stronger <= 0.0)
        return result;

    // The page has to carry lines of text, not a rule or a spot of ink.
    const int lines = rowVariance > colVariance ? countLines(m_rowInk) : countLines(m_colInk);
    if (lines < kMinLines)
        return result;
    result.textLike = true;

    // A profile that runs perfectly smooth (a page whose ink is the same in
    // every column) leaves the other one the only reading there is.
    if (weaker > 0.0 && stronger / weaker < kAxisMargin)
        return result;

    result.axisKnown = true;
    result.sideways = colVariance > rowVariance;
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
