#include "app/BlockTextHighlighter.h"

namespace llocr {

BlockTextHighlighter::BlockTextHighlighter(QObject *parent) : QSyntaxHighlighter(parent) {}

void BlockTextHighlighter::setStart(int start)
{
    if (m_start == start)
        return;
    m_start = start;
    rehighlightIfDocumented();
    emit rangeChanged();
}

void BlockTextHighlighter::setLength(int length)
{
    if (m_length == length)
        return;
    m_length = length;
    rehighlightIfDocumented();
    emit rangeChanged();
}

void BlockTextHighlighter::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    rehighlightIfDocumented();
    emit colorChanged();
}

void BlockTextHighlighter::highlightBlock(const QString &)
{
    if (m_length <= 0 || m_start < 0)
        return;

    const QTextBlock block = currentBlock();
    const int blockStart = block.position();
    const int from = qMax(m_start, blockStart);
    const int to = qMin(m_start + m_length, blockStart + block.length());
    if (from >= to)
        return;

    QTextCharFormat format;
    format.setBackground(m_color);
    setFormat(from - blockStart, to - from, format);
}

void BlockTextHighlighter::rehighlightIfDocumented()
{
    if (document())
        rehighlight();
}

}  // namespace llocr
