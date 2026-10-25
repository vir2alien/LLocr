#include "parsers/ChandraParser.h"
#include "parsers/BlockRender.h"

#include "parsers/HtmlMarkup.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace llocr {

namespace {

constexpr int kDiagnosticMinLength = 32;

// The label the app knows, from the label the model speaks.
const QHash<QString, QString> &labelMap()
{
    static const QHash<QString, QString> kMap = {{QStringLiteral("Text"), QStringLiteral("text")},
                                                 {QStringLiteral("Section-Header"), QStringLiteral("title")},
                                                 {QStringLiteral("Page-Header"), QStringLiteral("header")},
                                                 {QStringLiteral("Page-Footer"), QStringLiteral("footer")},
                                                 {QStringLiteral("Caption"), QStringLiteral("image_caption")},
                                                 {QStringLiteral("Footnote"), QStringLiteral("table_footnote")},
                                                 {QStringLiteral("Equation-Block"), QStringLiteral("equation")},
                                                 {QStringLiteral("List-Group"), QStringLiteral("list")},
                                                 {QStringLiteral("Image"), QStringLiteral("image")},
                                                 {QStringLiteral("Figure"), QStringLiteral("image")},
                                                 {QStringLiteral("Diagram"), QStringLiteral("image")},
                                                 {QStringLiteral("Table"), QStringLiteral("table")},
                                                 {QStringLiteral("Code-Block"), QStringLiteral("code")},
                                                 {QStringLiteral("Form"), QStringLiteral("key_value")},
                                                 {QStringLiteral("Table-Of-Contents"), QStringLiteral("list")},
                                                 {QStringLiteral("Complex-Block"), QStringLiteral("text")},
                                                 {QStringLiteral("Chemical-Block"), QStringLiteral("text")},
                                                 {QStringLiteral("Bibliography"), QStringLiteral("ref_text")}};
    return kMap;
}

QString appLabel(const QString &label)
{
    const auto found = labelMap().constFind(label);
    return found != labelMap().constEnd() ? *found : QStringLiteral("text");
}

// The reply the model wrapped in a code fence.
QString stripCodeFence(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith(QStringLiteral("```")))
        return text;
    const int openEnd = trimmed.indexOf(QLatin1Char('\n'));
    if (openEnd < 0)
        return text;
    const int closeStart = trimmed.lastIndexOf(QLatin1Char('\n'));
    if (closeStart <= openEnd || trimmed.mid(closeStart + 1).trimmed() != QStringLiteral("```"))
        return text;
    return trimmed.mid(openEnd + 1, closeStart - openEnd - 1);
}

// The blocks the model marked up: the divs that carry a box. They are the
// top-level ones; a reply that wraps them in a div of its own is read
// deeper.
QList<HtmlNode> layoutBlocks(const QList<HtmlNode> &nodes, bool deep)
{
    QList<HtmlNode> blocks;
    for (const HtmlNode &node : nodes) {
        if (node.isText())
            continue;
        if (node.tag == QLatin1String("div") && node.hasAttribute(QStringLiteral("data-bbox"))) {
            blocks.append(node);
            continue;
        }
        if (deep)
            blocks.append(layoutBlocks(node.children, true));
    }
    return blocks;
}

// The box of a block: four numbers in the model's own scale.
bool parseBbox(const QString &value, int range, int *coords)
{
    const QStringList parts = value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() != 4)
        return false;
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        const int number = parts.at(i).toInt(&ok);
        if (!ok || number < 0 || number > range)
            return false;
        coords[i] = number;
    }
    return true;
}

}  // namespace

QString ChandraParser::id() const
{
    return QStringLiteral("chandra-ocr");
}

QString ChandraParser::displayName() const
{
    return QStringLiteral("Chandra-OCR-2");
}

OcrResult ChandraParser::parse(const QString &rawText) const
{
    const QString reply = stripCodeFence(rawText).trimmed();
    if (reply.isEmpty())
        return OcrResult::makeError(QStringLiteral("Empty OCR text"));

    const QList<HtmlNode> nodes = parseHtml(reply);
    QList<HtmlNode> blocks;
    if (!m_options.ignoreLayoutMarkup) {
        blocks = layoutBlocks(nodes, false);
        if (blocks.isEmpty())
            blocks = layoutBlocks(nodes, true);
    }

    OcrResult result;
    OcrPage page;
    result.success = true;

    if (blocks.isEmpty()) {
        page.text = htmlToMarkdown(nodes).trimmed();
        result.text = page.text;
        result.pages.append(page);
        if (page.text.size() > kDiagnosticMinLength) {
            result.notes.append(QCoreApplication::translate("ChandraParser",
                                                            "No layout markup found in the model reply — the text was kept as "
                                                            "one block. Check that the OCR model and the output parser match."));
        }
        return result;
    }

    const int range = m_options.bboxRange > 0 ? m_options.bboxRange : 1000;
    for (const HtmlNode &node : blocks) {
        const QString label = node.attribute(QStringLiteral("data-label"));
        if (label == QLatin1String("Blank-Page"))
            continue;

        BoundingBox box;
        box.label = appLabel(label);
        box.text = htmlToMarkdown(node.children).trimmed();
        box.positioned = false;

        int coords[4] = {0, 0, 0, 0};
        if (parseBbox(node.attribute(QStringLiteral("data-bbox")), range, coords)) {
            const double x1 = (std::min)(coords[0], coords[2]) / double(range);
            const double y1 = (std::min)(coords[1], coords[3]) / double(range);
            const double x2 = (std::max)(coords[0], coords[2]) / double(range);
            const double y2 = (std::max)(coords[1], coords[3]) / double(range);
            box.rect = QRectF(x1, y1, x2 - x1, y2 - y1);
            box.positioned = true;
        }
        page.boxes.append(box);
    }

    page.text = rebuildText(page);
    result.text = page.text;
    result.pages.append(page);
    return result;
}

QString ChandraParser::rebuildText(const OcrPage &page) const
{
    if (page.boxes.isEmpty())
        return page.text;
    return rebuildTextWithRanges(page).text;
}

IOutputParser::RebuiltPageText ChandraParser::rebuildTextWithRanges(const OcrPage &page) const
{
    return renderPageText(page, m_options);
}

}  // namespace llocr
