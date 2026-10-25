#include "parsers/HtmlMarkup.h"

#include <QRegularExpression>
#include <QStringList>

namespace llocr {

namespace {

bool isVoidTag(const QString &tag)
{
    static const QStringList kVoid = {QStringLiteral("area"),
                                      QStringLiteral("base"),
                                      QStringLiteral("br"),
                                      QStringLiteral("col"),
                                      QStringLiteral("embed"),
                                      QStringLiteral("hr"),
                                      QStringLiteral("img"),
                                      QStringLiteral("input"),
                                      QStringLiteral("link"),
                                      QStringLiteral("meta"),
                                      QStringLiteral("source"),
                                      QStringLiteral("track"),
                                      QStringLiteral("wbr")};
    return kVoid.contains(tag);
}

bool isBlockTag(const QString &tag)
{
    static const QStringList kBlocks = {
        QStringLiteral("address"), QStringLiteral("article"), QStringLiteral("aside"),    QStringLiteral("blockquote"), QStringLiteral("dd"),     QStringLiteral("div"),
        QStringLiteral("dl"),      QStringLiteral("dt"),      QStringLiteral("fieldset"), QStringLiteral("figcaption"), QStringLiteral("figure"), QStringLiteral("footer"),
        QStringLiteral("form"),    QStringLiteral("h1"),      QStringLiteral("h2"),       QStringLiteral("h3"),         QStringLiteral("h4"),     QStringLiteral("h5"),
        QStringLiteral("h6"),      QStringLiteral("header"),  QStringLiteral("hr"),       QStringLiteral("li"),         QStringLiteral("main"),   QStringLiteral("nav"),
        QStringLiteral("ol"),      QStringLiteral("p"),       QStringLiteral("pre"),      QStringLiteral("section"),    QStringLiteral("table"),  QStringLiteral("tbody"),
        QStringLiteral("td"),      QStringLiteral("tfoot"),   QStringLiteral("th"),       QStringLiteral("thead"),      QStringLiteral("tr"),     QStringLiteral("ul")};
    return kBlocks.contains(tag);
}

// The end tag may be left out: the tag that comes next closes the open element.
bool implicitlyCloses(const QString &open, const QString &next)
{
    if (open == QLatin1String("p"))
        return isBlockTag(next) || next == QLatin1String("li");
    if (open == QLatin1String("li"))
        return next == QLatin1String("li");
    if (open == QLatin1String("td") || open == QLatin1String("th"))
        return next == QLatin1String("td") || next == QLatin1String("th") || next == QLatin1String("tr");
    if (open == QLatin1String("tr"))
        return next == QLatin1String("tr");
    if (open == QLatin1String("option"))
        return next == QLatin1String("option");
    return false;
}

QString escapeHtml(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

QString collapseWhitespace(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("[ \\t\\r\\n\\f\\v]+"));
    QString out = text;
    out.replace(re, QStringLiteral(" "));
    return out;
}

// The text of a subtree as it was written, without the markup.
QString rawText(const HtmlNode &node)
{
    if (node.isText())
        return node.text;
    QString out;
    for (const HtmlNode &child : node.children)
        out += rawText(child);
    return out;
}

QString mathText(const HtmlNode &node)
{
    const QString body = rawText(node).trimmed();
    if (body.isEmpty())
        return {};
    if (node.attribute(QStringLiteral("display")) == QLatin1String("block"))
        return QStringLiteral("$$\n%1\n$$").arg(body);
    return QStringLiteral("$%1$").arg(body);
}

QString checkboxMark(const HtmlNode &node)
{
    const QString type = node.attribute(QStringLiteral("type"));
    if (type != QLatin1String("checkbox") && type != QLatin1String("radio"))
        return {};
    return node.hasAttribute(QStringLiteral("checked")) ? QStringLiteral("☒") : QStringLiteral("☐");
}

QString serializeHtml(const HtmlNode &node)
{
    if (node.isText())
        return escapeHtml(node.text);

    QString out = QStringLiteral("<") + node.tag;
    for (auto it = node.attributes.constBegin(); it != node.attributes.constEnd(); ++it) {
        if (it.key() == QLatin1String("data-bbox") || it.key() == QLatin1String("data-label"))
            continue;
        out += QStringLiteral(" ") + it.key() + QStringLiteral("=\"") + escapeHtml(it.value()) + QStringLiteral("\"");
    }
    if (isVoidTag(node.tag))
        return out + QStringLiteral("/>");
    out += QLatin1Char('>');
    for (const HtmlNode &child : node.children)
        out += serializeHtml(child);
    return out + QStringLiteral("</") + node.tag + QStringLiteral(">");
}

bool hasBlockChild(const HtmlNode &node)
{
    for (const HtmlNode &child : node.children) {
        if (!child.isText() && isBlockTag(child.tag))
            return true;
    }
    return false;
}

QString renderInline(const QList<HtmlNode> &nodes);
QString renderBlock(const HtmlNode &node);
QString renderBlocks(const QList<HtmlNode> &nodes);

QString renderInlineNode(const HtmlNode &node)
{
    if (node.isText())
        return collapseWhitespace(node.text);

    const QString &tag = node.tag;
    if (tag == QLatin1String("br"))
        return QStringLiteral("\n");
    if (tag == QLatin1String("hr"))
        return QStringLiteral("\n---\n");
    if (tag == QLatin1String("img"))
        return node.attribute(QStringLiteral("alt"));
    if (tag == QLatin1String("input"))
        return checkboxMark(node);
    if (tag == QLatin1String("math"))
        return mathText(node);
    if (tag == QLatin1String("pre") || tag == QLatin1String("code"))
        return rawText(node);
    return renderInline(node.children);
}

QString renderInline(const QList<HtmlNode> &nodes)
{
    QString out;
    for (const HtmlNode &node : nodes)
        out += renderInlineNode(node);
    return out;
}

QString renderListItem(const HtmlNode &node)
{
    QString text;
    QStringList nested;
    for (const HtmlNode &child : node.children) {
        if (child.isText()) {
            text += collapseWhitespace(child.text);
            continue;
        }
        if (child.tag == QLatin1String("ul") || child.tag == QLatin1String("ol")) {
            nested.append(renderBlock(child));
            continue;
        }
        text += renderInlineNode(child);
    }
    text = text.trimmed();
    if (nested.isEmpty())
        return text;
    return text + QStringLiteral("\n") + nested.join(QStringLiteral("\n"));
}

QString renderList(const HtmlNode &node)
{
    QStringList items;
    int index = 1;
    for (const HtmlNode &child : node.children) {
        if (child.isText())
            continue;
        const QString content = (child.tag == QLatin1String("li") ? renderListItem(child) : renderBlock(child)).trimmed();
        if (content.isEmpty())
            continue;
        if (node.tag == QLatin1String("ol"))
            items.append(QStringLiteral("%1. %2").arg(index++).arg(content));
        else
            items.append(QStringLiteral("- %1").arg(content));
    }
    return items.join(QStringLiteral("\n"));
}

QString renderBlock(const HtmlNode &node)
{
    if (node.isText())
        return collapseWhitespace(node.text);

    const QString &tag = node.tag;
    // A heading carries its text only: the app's heading style writes the
    // markers, and the level it picks from the text.
    if (tag.size() == 2 && tag.at(0) == QLatin1Char('h') && tag.at(1).isDigit())
        return renderInline(node.children).trimmed();
    if (tag == QLatin1String("ul") || tag == QLatin1String("ol"))
        return renderList(node);
    if (tag == QLatin1String("li"))
        return renderListItem(node);
    if (tag == QLatin1String("table"))
        return serializeHtml(node);
    if (tag == QLatin1String("pre") || tag == QLatin1String("code"))
        return rawText(node);
    if (tag == QLatin1String("math"))
        return mathText(node);
    if (tag == QLatin1String("img"))
        return node.attribute(QStringLiteral("alt"));
    if (tag == QLatin1String("input"))
        return checkboxMark(node);
    if (tag == QLatin1String("hr"))
        return QStringLiteral("---");
    if (tag == QLatin1String("br"))
        return {};
    if (hasBlockChild(node))
        return renderBlocks(node.children);
    return renderInline(node.children);
}

QString renderBlocks(const QList<HtmlNode> &nodes)
{
    QStringList blocks;
    for (const HtmlNode &node : nodes) {
        const QString rendered = renderBlock(node).trimmed();
        if (!rendered.isEmpty())
            blocks.append(rendered);
    }
    return blocks.join(QStringLiteral("\n\n"));
}

class HtmlReader
{
public:
    explicit HtmlReader(const QString &html) : m_html(html) {}

    QList<HtmlNode> read() { return readNodes(QString()); }

private:
    QList<HtmlNode> readNodes(const QString &parentTag);
    HtmlNode readStartTag(const QString &body);
    static void appendText(QList<HtmlNode> &nodes, const QString &text);

    const QString &m_html;
    int m_pos = 0;
};

void HtmlReader::appendText(QList<HtmlNode> &nodes, const QString &text)
{
    if (text.isEmpty())
        return;
    HtmlNode node;
    node.text = decodeHtmlEntities(text);
    nodes.append(node);
}

HtmlNode HtmlReader::readStartTag(const QString &body)
{
    HtmlNode node;
    int i = 0;
    while (i < body.size() && !body.at(i).isSpace())
        ++i;
    node.tag = body.left(i).toLower();
    if (node.tag.isEmpty())
        return node;

    while (i < body.size()) {
        while (i < body.size() && (body.at(i).isSpace() || body.at(i) == QLatin1Char('/')))
            ++i;
        if (i >= body.size())
            break;

        const int nameStart = i;
        while (i < body.size() && !body.at(i).isSpace() && body.at(i) != QLatin1Char('='))
            ++i;
        const QString name = body.mid(nameStart, i - nameStart).toLower();

        QString value;
        while (i < body.size() && body.at(i).isSpace())
            ++i;
        if (i < body.size() && body.at(i) == QLatin1Char('=')) {
            ++i;
            while (i < body.size() && body.at(i).isSpace())
                ++i;
            if (i < body.size() && (body.at(i) == QLatin1Char('"') || body.at(i) == QLatin1Char('\''))) {
                const QChar quote = body.at(i++);
                const int valueStart = i;
                while (i < body.size() && body.at(i) != quote)
                    ++i;
                value = body.mid(valueStart, i - valueStart);
                if (i < body.size())
                    ++i;
            } else {
                const int valueStart = i;
                while (i < body.size() && !body.at(i).isSpace())
                    ++i;
                value = body.mid(valueStart, i - valueStart);
            }
        }
        if (!name.isEmpty())
            node.attributes.insert(name, decodeHtmlEntities(value));
    }
    return node;
}

QList<HtmlNode> HtmlReader::readNodes(const QString &parentTag)
{
    QList<HtmlNode> nodes;
    while (m_pos < m_html.size()) {
        const int lt = m_html.indexOf(QLatin1Char('<'), m_pos);
        if (lt < 0) {
            appendText(nodes, m_html.mid(m_pos));
            m_pos = m_html.size();
            break;
        }
        if (lt > m_pos) {
            appendText(nodes, m_html.mid(m_pos, lt - m_pos));
            m_pos = lt;
        }

        if (m_html.mid(m_pos, 4) == QLatin1String("<!--")) {
            const int end = m_html.indexOf(QStringLiteral("-->"), m_pos);
            m_pos = end < 0 ? m_html.size() : end + 3;
            continue;
        }
        if (m_html.mid(m_pos, 2) == QLatin1String("<!")) {
            const int end = m_html.indexOf(QLatin1Char('>'), m_pos);
            m_pos = end < 0 ? m_html.size() : end + 1;
            continue;
        }
        if (m_html.mid(m_pos, 2) == QLatin1String("</")) {
            const int end = m_html.indexOf(QLatin1Char('>'), m_pos);
            const QString name = end < 0 ? QString() : m_html.mid(m_pos + 2, end - m_pos - 2).trimmed().toLower();
            m_pos = end < 0 ? m_html.size() : end + 1;
            if (name == parentTag)
                return nodes;
            continue;
        }

        const int end = m_html.indexOf(QLatin1Char('>'), m_pos);
        if (end < 0) {
            appendText(nodes, m_html.mid(m_pos));
            m_pos = m_html.size();
            break;
        }
        const HtmlNode node = readStartTag(m_html.mid(m_pos + 1, end - m_pos - 1));
        if (node.tag.isEmpty()) {
            m_pos = end + 1;
            continue;
        }
        if (!parentTag.isEmpty() && implicitlyCloses(parentTag, node.tag))
            return nodes;

        m_pos = end + 1;
        HtmlNode element = node;
        if (!isVoidTag(element.tag))
            element.children = readNodes(element.tag);
        nodes.append(element);
    }
    return nodes;
}

}  // namespace

QString decodeHtmlEntities(const QString &text)
{
    static const QHash<QString, QString> kNamed = {{QStringLiteral("amp"), QStringLiteral("&")},
                                                   {QStringLiteral("lt"), QStringLiteral("<")},
                                                   {QStringLiteral("gt"), QStringLiteral(">")},
                                                   {QStringLiteral("quot"), QStringLiteral("\"")},
                                                   {QStringLiteral("apos"), QStringLiteral("'")},
                                                   {QStringLiteral("nbsp"), QStringLiteral(" ")},
                                                   {QStringLiteral("hellip"), QStringLiteral("…")},
                                                   {QStringLiteral("mdash"), QStringLiteral("—")},
                                                   {QStringLiteral("ndash"), QStringLiteral("–")}};
    static const QRegularExpression re(QStringLiteral("&(#x[0-9A-Fa-f]+|#[0-9]+|[A-Za-z][A-Za-z0-9]*);"));

    QString out;
    out.reserve(text.size());
    int pos = 0;
    auto matches = re.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        out += text.mid(pos, match.capturedStart() - pos);
        const QString entity = match.captured(1);
        bool ok = false;
        if (entity.startsWith(QLatin1String("#x"), Qt::CaseInsensitive)) {
            const char32_t code = static_cast<char32_t>(entity.mid(2).toUInt(&ok, 16));
            out += ok ? QString::fromUcs4(&code, 1) : match.captured(0);
        } else if (entity.startsWith(QLatin1Char('#'))) {
            const char32_t code = static_cast<char32_t>(entity.mid(1).toUInt(&ok, 10));
            out += ok ? QString::fromUcs4(&code, 1) : match.captured(0);
        } else {
            const auto named = kNamed.constFind(entity);
            out += named != kNamed.constEnd() ? *named : match.captured(0);
        }
        pos = match.capturedEnd();
    }
    out += text.mid(pos);
    return out;
}

QList<HtmlNode> parseHtml(const QString &html)
{
    return HtmlReader(html).read();
}

QString htmlToMarkdown(const QList<HtmlNode> &nodes)
{
    return renderBlocks(nodes);
}

QString htmlToMarkdown(const QString &html)
{
    return htmlToMarkdown(parseHtml(html));
}

}  // namespace llocr
