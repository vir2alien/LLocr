#include "parsers/MinerULayoutParser.h"

#include <algorithm>

namespace llocr {

namespace {

const QRegularExpression &layoutLineRegex()
{
    static const QRegularExpression re(
        QStringLiteral(R"(<\|box_start\|>\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*<\|box_end\|>\s*<\|ref_start\|>(\w+?)<\|ref_end\|>(?:\s*<\|rotate_(up|down|left|right)\|>)?)"));
    return re;
}

const QRegularExpression &canonicalTokenRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\])"));
    return re;
}

// Composite regions are recognition units, not content: the upstream client
// drops the layout children a container covers (cover ratio of the child
// >= 0.9, mineru-vl-utils' INTERNAL_BLOCK_THRESHOLD) and reads the container
// as one crop, so the same pixels are never recognized twice. The multi-line
// equation container gets the same treatment — its per-line children are the
// layout model's own segmentation of the formula the container holds — and
// so does the bullet list, whose items arrive as sibling text blocks.
constexpr double kCoveredRatio = 0.9;

qreal coveredRatio(const QRectF &inner, const QRectF &outer)
{
    const qreal innerArea = inner.width() * inner.height();
    if (innerArea <= 0)
        return 0;
    const QRectF common = inner.intersected(outer);
    return common.width() * common.height() / innerArea;
}

bool absorbs(const QString &container, const QString &candidate)
{
    if (container == QLatin1String("table") || container == QLatin1String("equation_block"))
        return candidate == QLatin1String("text") || candidate == QLatin1String("equation") || candidate == QLatin1String("equation_block");
    if (container == QLatin1String("image_block"))
        return candidate == QLatin1String("image") || candidate == QLatin1String("chart") || candidate == QLatin1String("image_caption");
    if (container == QLatin1String("list"))
        return candidate == QLatin1String("text") || candidate == QLatin1String("list_item");
    return false;
}

void absorbCoveredBlocks(QList<BoundingBox> &boxes)
{
    QList<BoundingBox> kept;
    kept.reserve(boxes.size());
    for (int i = 0; i < boxes.size(); ++i) {
        const QRectF &candidate = boxes.at(i).rect;
        const qreal candidateArea = candidate.width() * candidate.height();
        bool covered = false;
        for (int j = 0; j < boxes.size() && !covered; ++j) {
            if (i == j)
                continue;
            const QRectF &container = boxes.at(j).rect;
            // The container must be strictly larger: two near-identical boxes
            // then keep each other alive instead of erasing the pair.
            if (container.width() * container.height() <= candidateArea)
                continue;
            covered = absorbs(boxes.at(j).label, boxes.at(i).label) && coveredRatio(candidate, container) >= kCoveredRatio;
        }
        if (!covered)
            kept.append(boxes.at(i));
    }
    boxes = std::move(kept);
}

}  // namespace

QString MinerULayoutParser::id() const
{
    return QStringLiteral("mineru-layout");
}

QString MinerULayoutParser::displayName() const
{
    return QStringLiteral("MinerU layout");
}

OcrResult MinerULayoutParser::parse(const QString &rawText) const
{
    OcrResult result = DetTokenParserBase::parse(rawText);
    if (result.pages.isEmpty())
        return result;

    OcrPage &page = result.pages.first();
    // The rotation token the reply carries after every block (ADR 245): the
    // extraction walks the same lines prepareText keeps, so the zipping onto
    // the base's boxes only fires when the two lists line up — any surprise
    // (a preamble, a filtered line) leaves the blocks unmarked and the
    // page-level vote inert.
    struct LayoutLine {
        QString label;
        int orientation = 0;
    };
    QList<LayoutLine> lines;
    QRegularExpressionMatchIterator it = layoutLineRegex().globalMatch(rawText);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool valid = true;
        int coords[4];
        for (int i = 0; i < 4; ++i) {
            coords[i] = m.captured(i + 1).toInt(&valid);
            if (!valid || coords[i] < 0 || coords[i] > 1000) {
                valid = false;
                break;
            }
        }
        if (!valid || coords[0] == coords[2] || coords[1] == coords[3])
            continue;
        const QString label = m.captured(5);
        if (label == QLatin1String("inline_formula"))
            continue;
        lines.append({label, orientationDegrees(m.captured(6))});
    }
    // The vote reads the reply's lines directly — the page-level advice must
    // not depend on the box zip lining up.
    QList<int> orientations;
    orientations.reserve(lines.size());
    for (const LayoutLine &line : lines)
        orientations.append(line.orientation);
    const auto [voteTurn, voteShare] = orientationVote(orientations);
    page.orientationVote = voteTurn;
    page.orientationVoteShare = voteShare;
    const int kept = m_options.keepPageNumbers ? lines.size() : int(std::count_if(lines.cbegin(), lines.cend(), [](const LayoutLine &line) { return line.label != QLatin1String("page_number"); }));
    // The base prepends a preamble box when the reply opens with text: a
    // mismatch means the lists do not line up and nothing is zipped.
    if (kept == page.boxes.size()) {
        int boxIndex = 0;
        for (const LayoutLine &line : lines) {
            if (!m_options.keepPageNumbers && line.label == QLatin1String("page_number"))
                continue;
            page.boxes[boxIndex].orientation = line.orientation;
            ++boxIndex;
        }
    }

    absorbCoveredBlocks(page.boxes);
    for (BoundingBox &box : page.boxes) {
        if (box.label == QLatin1String("equation_block"))
            box.label = QStringLiteral("equation");
        else if (box.label == QLatin1String("image_block"))
            box.label = QStringLiteral("image");
    }
    page.text = rebuildText(page);
    result.text = page.text;
    return result;
}

QString MinerULayoutParser::prepareText(const QString &rawText) const
{
    QRegularExpressionMatchIterator it = layoutLineRegex().globalMatch(rawText);
    if (!it.hasNext())
        return rawText;

    QString out;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool valid = true;
        int coords[4];
        for (int i = 0; i < 4; ++i) {
            coords[i] = m.captured(i + 1).toInt(&valid);
            if (!valid || coords[i] < 0 || coords[i] > 1000) {
                valid = false;
                break;
            }
        }
        if (!valid)
            continue;

        const int x1 = coords[0];
        const int y1 = coords[1];
        const int x2 = coords[2];
        const int y2 = coords[3];
        if (x1 == x2 || y1 == y2)
            continue;

        QString label = m.captured(5);
        if (label == QLatin1String("inline_formula"))
            continue;
        if (label == QLatin1String("unknown"))
            label = QStringLiteral("image");
        out += QStringLiteral("%1 [%2,%3,%4,%5]\n").arg(label).arg(x1).arg(y1).arg(x2).arg(y2);
    }
    return out;
}

const QRegularExpression &MinerULayoutParser::tokenRegex(const QString &) const
{
    return canonicalTokenRegex();
}

bool MinerULayoutParser::escapesLineBreaks(const QString &) const
{
    return false;
}

}  // namespace llocr
