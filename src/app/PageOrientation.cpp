#include "app/PageOrientation.h"

#include <QVector>

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace llocr {
namespace {

// The check works on a small copy: the lines only have to be told apart, and
// a page-sized picture would cost more than the answer is worth.
constexpr int kWorkingEdge = 1200;

// A page that is nearly empty or nearly black carries no readable text.
constexpr double kMinInkFraction = 0.004;
constexpr double kMaxInkFraction = 0.45;

// The outer strip of the frame is ignored: the borders of the scanner, the
// dark edges and the shadow of the spine carry no text but weigh on every
// profile as if they did.
constexpr double kMarginFraction = 0.02;

// The window and the sensitivity of the local threshold: the threshold
// follows the illumination of the neighborhood, so a gradient or a shadow
// does not flip whole halves of the page from paper to ink.
constexpr int kSauvolaRadius = 15;
constexpr double kSauvolaK = 0.25;
constexpr double kSauvolaR = 128.0;

// A profile bin marks ink when at least this fraction of the other side of
// the page carries it; rows and columns below the line are margins.
constexpr double kCropInkFraction = 0.01;

// The morphological closing that bridges the gaps between the letters of a
// line, and the shape of what counts as a line afterwards: a component at
// least a closing long and eight times longer than it is thick.
constexpr int kCloseLength = 25;
constexpr double kLineAspect = 8.0;
constexpr int kMinLineComponentLength = 25;
// Fewer line-like components than this and the page is a photograph or a
// drawing, not text: the answer is a refusal, not a guess.
constexpr int kMinLineComponents = 10;

// The skew sweep: a scan with an automatic document feeder or a phone
// photograph is skewed by a degree or three, and an uncompensated skew
// smears the lines over the profile until the periodicity is gone.
constexpr double kSkewSweepDegrees = 15.0;
constexpr double kSkewStepDegrees = 0.5;

// The expected pitch of the lines, in pixels of the working copy: wide
// enough for the type sizes of a document, narrow enough that the slow
// profile of the margins and the columns cannot pass for lines. A lag peak
// at two or three line pitches on the other axis is the signature of a
// book layout, not of a page on its side.
constexpr int kMinLinePitch = 12;
constexpr int kMaxLinePitch = 60;

// The axis decision is absolute, not a ratio. The floor sits above the
// autocorrelation of noise (~0.07–0.13 measured on the corpus) and below
// the weakest real repetition of it (irregular pages of equations and
// tables carry their lines at 0.15–0.25).
constexpr double kMinPeriodicity = 0.15;

// A profile qualifies as the lines of the page when it alternates deeply
// enough: the profile across the lines of text dives from the ink of a line
// to the paper of the gap and back, and its variation stays above this; the
// profile along the text never empties — every column of a text block
// carries ink — and its variation stays below. Measured on the corpus: the
// line profiles at 0.74–1.43, the cross profiles at 0.15–0.59.
constexpr double kLineProfileVariation = 0.65;

// The threshold between the ink and the paper, chosen the way Otsu does it:
// the split with the widest gap between the two classes. It drives only the
// empty-page and photograph gate; the geometry is read from the local
// (Sauvola) mask.
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

QVector<uchar> sauvolaMask(const QImage &gray)
{
    const int w = gray.width();
    const int h = gray.height();
    std::vector<double> sum(size_t(w + 1) * (h + 1), 0.0);
    std::vector<double> sumSq(size_t(w + 1) * (h + 1), 0.0);
    for (int y = 0; y < h; ++y) {
        const uchar *line = gray.constScanLine(y);
        const double *sumPrev = sum.data() + size_t(y) * (w + 1);
        double *sumCur = sum.data() + size_t(y + 1) * (w + 1);
        const double *sumSqPrev = sumSq.data() + size_t(y) * (w + 1);
        double *sumSqCur = sumSq.data() + size_t(y + 1) * (w + 1);
        for (int x = 0; x < w; ++x) {
            const double v = line[x];
            sumCur[x + 1] = v + sumPrev[x + 1] + sumCur[x] - sumPrev[x];
            sumSqCur[x + 1] = v * v + sumSqPrev[x + 1] + sumSqCur[x] - sumSqPrev[x];
        }
    }

    QVector<uchar> mask(w * h, 0);
    for (int y = 0; y < h; ++y) {
        const uchar *line = gray.constScanLine(y);
        uchar *out = mask.data() + size_t(y) * w;
        const int y0 = std::max(0, y - kSauvolaRadius);
        const int y1 = std::min(h - 1, y + kSauvolaRadius);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - kSauvolaRadius);
            const int x1 = std::min(w - 1, x + kSauvolaRadius);
            const double area = double((x1 - x0 + 1) * (y1 - y0 + 1));
            const double s = sum[size_t(y1 + 1) * (w + 1) + (x1 + 1)] - sum[size_t(y0) * (w + 1) + (x1 + 1)] - sum[size_t(y1 + 1) * (w + 1) + x0] + sum[size_t(y0) * (w + 1) + x0];
            const double s2 = sumSq[size_t(y1 + 1) * (w + 1) + (x1 + 1)] - sumSq[size_t(y0) * (w + 1) + (x1 + 1)] - sumSq[size_t(y1 + 1) * (w + 1) + x0] + sumSq[size_t(y0) * (w + 1) + x0];
            const double mean = s / area;
            const double variance = std::max(0.0, s2 / area - mean * mean);
            const double stdDev = std::sqrt(variance);
            const double threshold = mean * (1.0 + kSauvolaK * (stdDev / kSauvolaR - 1.0));
            if (line[x] < threshold)
                out[x] = 1;
        }
    }
    return mask;
}

void stripMargin(QVector<uchar> &mask, int w, int h)
{
    const int mx = int(w * kMarginFraction);
    const int my = int(h * kMarginFraction);
    for (int y = 0; y < h; ++y) {
        uchar *line = mask.data() + size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            if (x < mx || x >= w - mx || y < my || y >= h - my)
                line[x] = 0;
        }
    }
}

struct Profiles {
    QVector<int> rows;
    QVector<int> cols;
    int total = 0;
};

Profiles buildProfiles(const QVector<uchar> &mask, int w, int h)
{
    Profiles profile;
    profile.rows.assign(h, 0);
    profile.cols.assign(w, 0);
    for (int y = 0; y < h; ++y) {
        const uchar *line = mask.constData() + size_t(y) * w;
        int rowTotal = 0;
        for (int x = 0; x < w; ++x) {
            if (!line[x])
                continue;
            ++rowTotal;
            ++profile.cols[x];
        }
        profile.rows[y] = rowTotal;
        profile.total += rowTotal;
    }
    return profile;
}

// Cuts the mask down to the bounding box of the ink: the margins carry no
// lines, and every bin of them a profile keeps is a bin the periodicity has
// to see through.
QVector<uchar> cropToInk(const QVector<uchar> &mask, int w, int h, int &cw, int &ch)
{
    cw = 0;
    ch = 0;
    const Profiles profile = buildProfiles(mask, w, h);
    const double rowFloor = std::max(1.0, kCropInkFraction * w);
    const double colFloor = std::max(1.0, kCropInkFraction * h);
    int top = 0;
    int bottom = h - 1;
    while (top <= bottom && profile.rows[top] < rowFloor)
        ++top;
    while (bottom >= top && profile.rows[bottom] < rowFloor)
        --bottom;
    int left = 0;
    int right = w - 1;
    while (left <= right && profile.cols[left] < colFloor)
        ++left;
    while (right >= left && profile.cols[right] < colFloor)
        --right;
    if (top > bottom || left > right)
        return {};

    cw = right - left + 1;
    ch = bottom - top + 1;
    QVector<uchar> cropped(size_t(cw) * ch);
    for (int y = 0; y < ch; ++y)
        std::memcpy(cropped.data() + size_t(y) * cw, mask.constData() + size_t(y + top) * w + left, cw);
    return cropped;
}

// Closing with a 1×len (or len×1) bar, run by run: the ink of a line comes
// in stretches, a stretch dilates by growing each run by the radius on both
// sides and merging what meets, and the erosion cuts every grown run back by
// the radius. What survives is the ink a full bar fits into — the same mask
// the pixel-wise dilate-then-erode produces, read from the runs instead of
// the pixels.
void closeRuns(QVector<uchar> &mask, int w, int h, int len, bool horizontal)
{
    const int outer = horizontal ? h : w;
    const int inner = horizontal ? w : h;
    const int radius = len / 2;
    const int stride = horizontal ? 1 : w;
    QVector<uchar> closed(mask.size(), 0);
    std::vector<int> runStarts;
    runStarts.reserve(64);
    for (int o = 0; o < outer; ++o) {
        const uchar *line = mask.constData() + size_t(o) * (horizontal ? w : 1);
        runStarts.clear();
        for (int i = 0; i < inner; ++i) {
            if (line[i * stride] && (i == 0 || !line[(i - 1) * stride]))
                runStarts.push_back(i);
        }
        uchar *out = closed.data() + size_t(o) * (horizontal ? w : 1);
        for (int run = 0; run < int(runStarts.size()); ++run) {
            const int start = runStarts[run];
            int end = start;
            while (end + 1 < inner && line[(end + 1) * stride])
                ++end;
            // The grown run, merged with every run it now touches.
            const int from = std::max(0, start - radius);
            int to = std::min(inner - 1, end + radius);
            int next = run + 1;
            while (next < int(runStarts.size()) && runStarts[next] <= to + radius + 1) {
                int nextEnd = runStarts[next];
                while (nextEnd + 1 < inner && line[(nextEnd + 1) * stride])
                    ++nextEnd;
                to = std::min(inner - 1, std::max(to, nextEnd + radius));
                ++next;
            }
            run = next - 1;
            // Erosion: the bar has to fit inside the grown run.
            for (int i = from + radius; i <= to - radius; ++i)
                out[i * stride] = 1;
        }
    }
    mask = std::move(closed);
}

struct ComponentCounter {
    std::vector<int> parent;
    std::vector<int> minX, maxX, minY, maxY;

    int find(int label)
    {
        while (parent[label] != label)
            label = parent[label] = parent[parent[label]];
        return label;
    }
};

// The number of line-shaped components of the mask: the stretches of a page
// of text close into dozens of them, a photograph or a rule into none.
int countLineComponents(const QVector<uchar> &mask, int w, int h)
{
    if (w <= 0 || h <= 0)
        return 0;
    ComponentCounter counter;
    counter.parent.push_back(0);  // label 0 is "no component"
    counter.minX.push_back(0);
    counter.maxX.push_back(0);
    counter.minY.push_back(0);
    counter.maxY.push_back(0);
    std::vector<int> labels(size_t(w) * h, 0);
    int next = 1;
    for (int y = 0; y < h; ++y) {
        const uchar *line = mask.constData() + size_t(y) * w;
        int *above = y > 0 ? labels.data() + size_t(y - 1) * w : nullptr;
        int *current = labels.data() + size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            if (!line[x])
                continue;
            const int left = x > 0 ? current[x - 1] : 0;
            const int up = above ? above[x] : 0;
            if (left && up) {
                const int a = counter.find(left);
                const int b = counter.find(up);
                if (a != b)
                    counter.parent[std::max(a, b)] = std::min(a, b);
                current[x] = std::min(a, b);
            } else if (left || up) {
                current[x] = std::max(left, up);
            } else {
                current[x] = next;
                counter.parent.push_back(next);
                counter.minX.push_back(x);
                counter.maxX.push_back(x);
                counter.minY.push_back(y);
                counter.maxY.push_back(y);
                ++next;
            }
        }
    }
    for (int y = 0; y < h; ++y) {
        const int *current = labels.data() + size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            if (!current[x])
                continue;
            const int root = counter.find(current[x]);
            counter.minX[root] = std::min(counter.minX[root], x);
            counter.maxX[root] = std::max(counter.maxX[root], x);
            counter.minY[root] = std::min(counter.minY[root], y);
            counter.maxY[root] = std::max(counter.maxY[root], y);
        }
    }
    int lines = 0;
    for (int label = 1; label < next; ++label) {
        if (counter.find(label) != label)
            continue;
        const int width = counter.maxX[label] - counter.minX[label] + 1;
        const int height = counter.maxY[label] - counter.minY[label] + 1;
        const int longSide = std::max(width, height);
        const int shortSide = std::min(width, height);
        if (longSide >= kMinLineComponentLength && longSide >= kLineAspect * shortSide)
            ++lines;
    }
    return lines;
}

struct SkewReading {
    double degrees = 0.0;  ///< The rotation that sharpens the profile most.
    int axis = 0;          ///< 0: the lines run across (rows); 1: up and down (columns).
};

// The sweep for the skew: every candidate rotation piles the ink into row
// and column profiles, and the skew is the rotation under which one of the
// two profiles gets its lines sharpest — the sum of the squared steps of
// the profile grows when the lines line up with the bins. Read on a halved
// copy: the angle does not care about the scale, and the refined angle is
// re-read once at full size.
SkewReading sweepSkew(const QVector<uchar> &mask, int w, int h)
{
    const int hw = (w + 1) / 2;
    const int hh = (h + 1) / 2;
    struct Point {
        int x, y;
    };
    std::vector<Point> points;
    points.reserve(size_t(hw) * hh / 4);
    for (int y = 0; y < h; y += 2) {
        const uchar *line = mask.constData() + size_t(y) * w;
        for (int x = 0; x < w; x += 2) {
            if (line[x])
                points.push_back({x / 2, y / 2});
        }
    }
    if (points.size() < 200)
        return {};

    const double pad = (hw + hh) / 2.0 + 2.0;
    QVector<int> rowAcc(int(hh + 2 * pad), 0);
    QVector<int> colAcc(int(hw + 2 * pad), 0);
    const int angleSteps = int(kSkewSweepDegrees / kSkewStepDegrees);
    double bestRowScore = -1.0;
    double bestColScore = -1.0;
    int bestRowStep = 0;
    int bestColStep = 0;
    const double total = double(points.size());

    auto sharpness = [](const QVector<int> &bins, int from, int to) {
        double sum = 0.0;
        for (int i = from; i < to; ++i) {
            const double step = bins[i + 1] - bins[i];
            sum += step * step;
        }
        return sum;
    };

    for (int step = -angleSteps; step <= angleSteps; ++step) {
        const double radians = qDegreesToRadians(step * kSkewStepDegrees);
        const double cosA = std::cos(radians);
        const double sinA = std::sin(radians);
        rowAcc.fill(0);
        colAcc.fill(0);
        const double cx = hw / 2.0;
        const double cy = hh / 2.0;
        for (const Point &p : points) {
            const double dx = p.x - cx;
            const double dy = p.y - cy;
            const double xr = cx + dx * cosA - dy * sinA;
            const double yr = cy + dx * sinA + dy * cosA;
            ++rowAcc[int(std::floor(yr)) + int(pad)];
            ++colAcc[int(std::floor(xr)) + int(pad)];
        }
        const double rowScore = sharpness(rowAcc, int(pad) - 2, int(pad) + hh + 2) / (total * total);
        const double colScore = sharpness(colAcc, int(pad) - 2, int(pad) + hw + 2) / (total * total);
        if (rowScore > bestRowScore) {
            bestRowScore = rowScore;
            bestRowStep = step;
        }
        if (colScore > bestColScore) {
            bestColScore = colScore;
            bestColStep = step;
        }
    }

    // A parabola through the winner and its two neighbors narrows the step
    // of the sweep down to a fraction of a degree.
    auto refine = [&](int bestStep, bool rows) {
        auto scoreAt = [&](double degrees) {
            const double a = qDegreesToRadians(degrees);
            const double cosA = std::cos(a);
            const double sinA = std::sin(a);
            rowAcc.fill(0);
            colAcc.fill(0);
            const double cx = hw / 2.0;
            const double cy = hh / 2.0;
            for (const Point &p : points) {
                const double dx = p.x - cx;
                const double dy = p.y - cy;
                const double xr = cx + dx * cosA - dy * sinA;
                const double yr = cy + dx * sinA + dy * cosA;
                ++rowAcc[int(std::floor(yr)) + int(pad)];
                ++colAcc[int(std::floor(xr)) + int(pad)];
            }
            return rows ? sharpness(rowAcc, int(pad) - 2, int(pad) + hh + 2) / (total * total) : sharpness(colAcc, int(pad) - 2, int(pad) + hw + 2) / (total * total);
        };
        const double center = scoreAt(bestStep * kSkewStepDegrees);
        const double minus = scoreAt((bestStep - 1) * kSkewStepDegrees);
        const double plus = scoreAt((bestStep + 1) * kSkewStepDegrees);
        const double denominator = minus - 2.0 * center + plus;
        double refined = bestStep * kSkewStepDegrees;
        if (std::abs(denominator) > 1e-12) {
            const double offset = 0.5 * (minus - plus) / denominator * kSkewStepDegrees;
            refined += std::clamp(offset, -kSkewStepDegrees, kSkewStepDegrees);
        }
        return refined;
    };

    SkewReading reading;
    if (bestRowScore >= bestColScore) {
        reading.axis = 0;
        reading.degrees = refine(bestRowStep, true);
    } else {
        reading.axis = 1;
        reading.degrees = refine(bestColStep, false);
    }
    reading.degrees = std::clamp(reading.degrees, -kSkewSweepDegrees, kSkewSweepDegrees);
    return reading;
}

// The profile of one direction of the page turned by the correction angle:
// the single full-size pass the periodicity reads.
QVector<int> deskewedProfile(const QVector<uchar> &mask, int w, int h, double degrees, bool rows)
{
    const double radians = qDegreesToRadians(degrees);
    const double cosA = std::cos(radians);
    const double sinA = std::sin(radians);
    const double pad = (w + h) / 2.0 + 2.0;
    const int rowBins = int(h + 2 * pad);
    const int colBins = int(w + 2 * pad);
    QVector<int> rowAcc(rows ? rowBins : 0);
    QVector<int> colAcc(rows ? 0 : colBins);
    int from = int(rows ? rowAcc.size() : colAcc.size());
    int to = -1;
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    for (int y = 0; y < h; ++y) {
        const uchar *line = mask.constData() + size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            if (!line[x])
                continue;
            const double dx = x - cx;
            const double dy = y - cy;
            if (rows) {
                const double yr = cy + dx * sinA + dy * cosA;
                const int bin = int(std::floor(yr)) + int(pad);
                ++rowAcc[bin];
                from = std::min(from, bin);
                to = std::max(to, bin);
            } else {
                const double xr = cx + dx * cosA - dy * sinA;
                const int bin = int(std::floor(xr)) + int(pad);
                ++colAcc[bin];
                from = std::min(from, bin);
                to = std::max(to, bin);
            }
        }
    }
    if (to < from)
        return {};
    return rows ? rowAcc.mid(from, to - from + 1) : colAcc.mid(from, to - from + 1);
}

struct Periodicity {
    double value = 0.0;      ///< The strongest normalized autocorrelation in the pitch window.
    int pitch = 0;           ///< The lag of the strongest repetition, in bins.
    double variation = 0.0;  ///< Std-dev over mean of the profile: how deeply it alternates.
};

// How periodic the profile is: the strongest normalized autocorrelation
// within the expected pitch of the lines. r = 1 — perfect lines, r ~ 0 — a
// smooth profile or noise. This is what tells the lines from the margins:
// the margins vary slowly, the lines repeat.
Periodicity periodicity(const QVector<int> &profile, int minLag, int maxLag)
{
    const int n = profile.size();
    maxLag = std::min(maxLag, n / 4);
    if (n < 4 * minLag || maxLag < minLag)
        return {};

    double mean = 0.0;
    for (const int value : profile)
        mean += value;
    mean /= n;

    double zeroLag = 0.0;
    QVector<double> centred(n);
    for (int i = 0; i < n; ++i) {
        centred[i] = profile[i] - mean;
        zeroLag += centred[i] * centred[i];
    }
    if (zeroLag <= 1e-9)
        return {};

    Periodicity best;
    best.variation = mean > 0.0 ? std::sqrt(std::max(0.0, zeroLag / n)) / mean : 0.0;

    for (int lag = minLag; lag <= maxLag; ++lag) {
        double acc = 0.0;
        for (int i = 0; i + lag < n; ++i)
            acc += centred[i] * centred[i + lag];
        const double value = acc / zeroLag;
        if (value > best.value) {
            best.value = value;
            best.pitch = lag;
        }
    }
    best.variation = mean > 0.0 ? std::sqrt(std::max(0.0, zeroLag / n)) / mean : 0.0;
    best.value = qBound(0.0, best.value, 1.0);
    return best;
}

class PageScanner
{
public:
    explicit PageScanner(const QImage &image);

    OrientationVerdict verdict();

private:
    QImage m_gray;
    int m_width = 0;
    int m_height = 0;
    double m_otsuInkFraction = 0.0;
    QVector<uchar> m_ink;  // The local-threshold mask, margins stripped.
    int m_cw = 0;
    int m_ch = 0;
    QVector<uchar> m_cropped;  // The mask cut down to the ink.
};

PageScanner::PageScanner(const QImage &image)
{
    QImage scaled = image;
    const int longest = qMax(scaled.width(), scaled.height());
    if (longest > kWorkingEdge) {
        const QSize fit = QSize(scaled.width(), scaled.height()).scaled(QSize(kWorkingEdge, kWorkingEdge), Qt::KeepAspectRatio);
        scaled = scaled.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    m_gray = scaled.convertToFormat(QImage::Format_Grayscale8);
    m_width = m_gray.width();
    m_height = m_gray.height();
}

OrientationVerdict PageScanner::verdict()
{
    OrientationVerdict result;
    if (m_width <= 0 || m_height <= 0)
        return result;

    // The empty-page and photograph gate, on the global split it has always
    // been read from.
    const int threshold = otsuThreshold(m_gray);
    int otsuInk = 0;
    for (int y = 0; y < m_height; ++y) {
        const uchar *line = m_gray.constScanLine(y);
        for (int x = 0; x < m_width; ++x)
            otsuInk += line[x] <= threshold;
    }
    m_otsuInkFraction = double(otsuInk) / double(m_width * m_height);
    if (m_otsuInkFraction < kMinInkFraction || m_otsuInkFraction > kMaxInkFraction)
        return result;

    m_ink = sauvolaMask(m_gray);
    stripMargin(m_ink, m_width, m_height);
    m_cropped = cropToInk(m_ink, m_width, m_height, m_cw, m_ch);
    if (m_cropped.isEmpty())
        return result;

    // The lines of the page close into long thin components whichever way
    // the page lies; a photograph closes into none worth counting. The
    // closed masks also feed the periodicity: the closing flattens a line
    // into one solid band, which sharpens the repetition of the lines and
    // smears the repetition of the letters.
    QVector<uchar> closedAcross = m_cropped;
    closeRuns(closedAcross, m_cw, m_ch, kCloseLength, true);
    QVector<uchar> closedAlong = m_cropped;
    closeRuns(closedAlong, m_cw, m_ch, kCloseLength, false);
    const int across = countLineComponents(closedAcross, m_cw, m_ch);
    const int along = countLineComponents(closedAlong, m_cw, m_ch);
    if (qEnvironmentVariableIsSet("LLOCR_ORIENTATION_DEBUG"))
        fprintf(stderr, "ODEBUG gates: otsu=%.4f ccAcross=%d ccAlong=%d crop=%dx%d\n", m_otsuInkFraction, across, along, m_cw, m_ch);
    if (across + along < kMinLineComponents)
        return result;
    result.textLike = true;

    const SkewReading skew = sweepSkew(m_cropped, m_cw, m_ch);
    const QVector<int> rowProfile = deskewedProfile(closedAcross, m_cw, m_ch, skew.degrees, true);
    const QVector<int> colProfile = deskewedProfile(closedAlong, m_cw, m_ch, skew.degrees, false);
    const Periodicity rows = periodicity(rowProfile, kMinLinePitch, kMaxLinePitch);
    const Periodicity cols = periodicity(colProfile, kMinLinePitch, kMaxLinePitch);

    // The axis is the profile that reads as the lines of the page: it dives
    // from the ink of a line to the paper of the gap and back, while the
    // profile across the text never empties. When both read as lines — a
    // contents page repeats along its entries and along their indents — the
    // deeper alternation is the lines: the line profile dives to paper, the
    // cross profile of a text block stays loaded.
    const bool rowLines = rows.variation >= kLineProfileVariation;
    const bool colLines = cols.variation >= kLineProfileVariation;
    if (!rowLines && !colLines)
        return result;
    const int axis = rowLines && colLines ? (rows.variation >= cols.variation ? 0 : 1) : (rowLines ? 0 : 1);
    const double firmness = axis == 0 ? rows.value : cols.value;
    result.confidence = firmness;
    if (firmness < kMinPeriodicity)
        return result;

    result.axisKnown = true;
    result.sideways = axis == 1;
    result.deskewDegrees = skew.degrees;
    if (qEnvironmentVariableIsSet("LLOCR_ORIENTATION_DEBUG"))
        fprintf(stderr,
                "ODEBUG %dx%d rows=%.3f@%d cv=%.2f cols=%.3f@%d cv=%.2f axis=%d conf=%.3f lines=%d\n",
                m_cw,
                m_ch,
                rows.value,
                rows.pitch,
                rows.variation,
                cols.value,
                cols.pitch,
                cols.variation,
                axis,
                result.confidence,
                across + along);
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
