#include "models/CropScaling.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QVector>

namespace llocr {

namespace {

constexpr int kVisionPatch = 14;
constexpr int kVisionMerge = 2;

// Below this the stretch buys the model nothing worth the resample blur.
constexpr qreal kMinGlyphUpscale = 1.05;

constexpr int kMinLetterHeight = 4;      // noise dots
constexpr qreal kMaxLetterHeight = 0.9;  // of the crop height: rules, borders
constexpr qreal kMaxLetterWidth = 0.6;   // of the crop width: fraction bars
constexpr int kMinLetterArea = 8;        // specks of dust

// The ink threshold moves between scans; the adaptive split also agrees with
// the fixed 128 the scale experiments measured on clean renders.
int otsuThreshold(const QImage &gray)
{
    int histogram[256] = {0};
    const qreal total = qreal(gray.width()) * gray.height();
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            ++histogram[line[x]];
    }
    qreal weightedSum = 0;
    for (int i = 0; i < 256; ++i)
        weightedSum += i * histogram[i];

    qreal backgroundSum = 0;
    qreal backgroundWeight = 0;
    qreal bestVariance = -1.0;
    int threshold = 128;
    for (int i = 0; i < 256; ++i) {
        backgroundWeight += histogram[i];
        if (backgroundWeight <= 0)
            continue;
        const qreal foregroundWeight = total - backgroundWeight;
        if (foregroundWeight <= 0)
            break;
        backgroundSum += i * histogram[i];
        const qreal backgroundMean = backgroundSum / backgroundWeight;
        const qreal foregroundMean = (weightedSum - backgroundSum) / foregroundWeight;
        const qreal spread = backgroundMean - foregroundMean;
        const qreal betweenVariance = backgroundWeight * foregroundWeight * spread * spread;
        if (betweenVariance > bestVariance) {
            bestVariance = betweenVariance;
            threshold = i + 1;  // the ink side is gray < threshold
        }
    }
    return threshold;
}

struct ComponentRun {
    int height = 0;
    int width = 0;
    int area = 0;
};

struct ComponentCounter {
    std::vector<int> parent;
    std::vector<int> minX, maxX, minY, maxY, area;

    int find(int label)
    {
        while (parent[label] != label)
            label = parent[label] = parent[parent[label]];
        return label;
    }
};

// 8-connected two-pass labeling: the diagonals keep an italic stroke or an
// accented letter one component, where four-connected splits them.
std::vector<ComponentRun> imageComponents(const QVector<uchar> &mask, int w, int h)
{
    ComponentCounter counter;
    counter.parent.push_back(0);  // label 0 is "no component"
    counter.minX.push_back(0);
    counter.maxX.push_back(0);
    counter.minY.push_back(0);
    counter.maxY.push_back(0);
    counter.area.push_back(0);
    std::vector<int> labels(size_t(w) * h, 0);
    int next = 1;
    for (int y = 0; y < h; ++y) {
        const uchar *line = mask.constData() + size_t(y) * w;
        const int *above = y > 0 ? labels.data() + size_t(y - 1) * w : nullptr;
        int *current = labels.data() + size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            if (!line[x])
                continue;
            const int left = x > 0 ? current[x - 1] : 0;
            const int upLeft = (above && x > 0) ? above[x - 1] : 0;
            const int up = above ? above[x] : 0;
            const int upRight = (above && x + 1 < w) ? above[x + 1] : 0;
            int root = 0;
            for (int neighbor : {left, upLeft, up, upRight}) {
                if (!neighbor)
                    continue;
                const int found = counter.find(neighbor);
                if (!root) {
                    root = found;
                } else if (found != root) {
                    counter.parent[std::max(root, found)] = std::min(root, found);
                    root = std::min(root, found);
                }
            }
            if (root) {
                current[x] = root;
            } else {
                current[x] = next;
                counter.parent.push_back(next);
                counter.minX.push_back(x);
                counter.maxX.push_back(x);
                counter.minY.push_back(y);
                counter.maxY.push_back(y);
                counter.area.push_back(0);
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
            ++counter.area[root];
        }
    }
    std::vector<ComponentRun> runs;
    for (int label = 1; label < next; ++label) {
        if (counter.find(label) != label || !counter.area[label])
            continue;
        runs.push_back({counter.maxY[label] - counter.minY[label] + 1, counter.maxX[label] - counter.minX[label] + 1, counter.area[label]});
    }
    return runs;
}

QSize scaledBy(const QSize &size, qreal scale)
{
    return QSize(qRound(size.width() * scale), qRound(size.height() * scale)).expandedTo(QSize(1, 1));
}

}  // namespace

CropScalingPolicy policyForRole(const ModelProfiles::Role *role)
{
    CropScalingPolicy policy;
    if (!role)
        return policy;
    policy.maxVisionTokens = role->cropMaxVisionTokens;
    policy.glyphHeightByBlock = role->cropGlyphHeights;
    return policy;
}

int estimateVisionTokens(const QSize &pixelSize)
{
    const int columns = (pixelSize.width() + kVisionPatch - 1) / kVisionPatch;
    const int rows = (pixelSize.height() + kVisionPatch - 1) / kVisionPatch;
    return (columns * rows) / (kVisionMerge * kVisionMerge);
}

int cropGlyphHeight(const QImage &crop)
{
    if (crop.isNull() || crop.width() < kMinLetterHeight || crop.height() < kMinLetterHeight)
        return 0;
    const QImage gray = crop.convertToFormat(QImage::Format_Grayscale8);
    const int threshold = otsuThreshold(gray);
    QVector<uchar> mask(size_t(gray.width()) * gray.height(), 0);
    bool anyInk = false;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *line = gray.constScanLine(y);
        uchar *maskLine = mask.data() + size_t(y) * gray.width();
        for (int x = 0; x < gray.width(); ++x) {
            maskLine[x] = line[x] < threshold;
            anyInk = anyInk || maskLine[x];
        }
    }
    if (!anyInk)
        return 0;
    const std::vector<ComponentRun> runs = imageComponents(mask, gray.width(), gray.height());
    std::vector<int> heights;
    for (const ComponentRun &run : runs) {
        if (run.height >= kMinLetterHeight && run.height <= kMaxLetterHeight * gray.height() && run.width <= kMaxLetterWidth * gray.width() && run.area >= kMinLetterArea)
            heights.push_back(run.height);
    }
    if (heights.empty())
        return 0;
    std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
    return heights[heights.size() / 2];
}

QImage applyCropScaling(const QImage &crop, const QString &blockLabel, const CropScalingPolicy &policy)
{
    if (crop.isNull())
        return crop;
    QImage scaled = crop;

    // The scale experiments: small print misread below a ~30 px letter height.
    // Only up — a label already at the target keeps its bytes.
    if (!policy.glyphHeightByBlock.isEmpty() && !blockLabel.isEmpty()) {
        const int target = policy.glyphHeightByBlock.value(blockLabel, 0);
        const int glyph = target > 0 ? cropGlyphHeight(scaled) : 0;
        if (glyph > 0 && glyph < target) {
            const qreal scale = qreal(target) / glyph;
            if (scale >= kMinGlyphUpscale)
                scaled = scaled.scaled(scaledBy(scaled.size(), scale), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }

    // Only down: the budget keeps the prompt inside the slot the server can
    // hold, never inflates a small crop. One pass may still land a few cells
    // over (the patch grid rounds every edge up), so the stage converges.
    if (policy.maxVisionTokens > 0) {
        for (int pass = 0; pass < 4; ++pass) {
            const int tokens = estimateVisionTokens(scaled.size());
            if (tokens <= policy.maxVisionTokens)
                break;
            const QSize next = scaledBy(scaled.size(), std::sqrt(qreal(policy.maxVisionTokens) / tokens));
            if (next == scaled.size())
                break;  // a one-pixel crop cannot shrink further
            scaled = scaled.scaled(next, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }
    return scaled;
}

}  // namespace llocr
