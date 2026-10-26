#pragma once

#include <QHash>
#include <QList>
#include <QString>

namespace llocr {

// One node of the HTML an OCR model answers with: an element with its
// attributes and children, or a run of text.
struct HtmlNode {
    QString tag;  // Empty for a text node.
    QHash<QString, QString> attributes;
    QString text;  // The text of a text node.
    QList<HtmlNode> children;
    bool selfClosing = false;  // The tag closed itself: "<br/>".

    bool isText() const { return tag.isEmpty(); }
    bool hasAttribute(const QString &name) const { return attributes.contains(name); }
    QString attribute(const QString &name, const QString &fallback = {}) const { return attributes.value(name, fallback); }
};

// The reply read as a tree. The end tag may be left out — the next tag of the
// same kind closes the open element — and a stray end tag is dropped.
QList<HtmlNode> parseHtml(const QString &html);

// The content as Markdown: paragraphs, headings, lists and math the way the
// app writes them, a table left as the HTML it came as.
QString htmlToMarkdown(const QList<HtmlNode> &nodes);
QString htmlToMarkdown(const QString &html);

// The entities of an HTML text resolved: &amp; becomes &, &#8212; becomes —.
QString decodeHtmlEntities(const QString &text);

}  // namespace llocr
