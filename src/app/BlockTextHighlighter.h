#pragma once

#include <QColor>
#include <QSyntaxHighlighter>

namespace llocr {

class BlockTextHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
    Q_PROPERTY(int start READ start WRITE setStart NOTIFY rangeChanged)
    Q_PROPERTY(int length READ length WRITE setLength NOTIFY rangeChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit BlockTextHighlighter(QObject *parent = nullptr);

    int start() const { return m_start; }
    void setStart(int start);

    int length() const { return m_length; }
    void setLength(int length);

    QColor color() const { return m_color; }
    void setColor(const QColor &color);

signals:
    void rangeChanged();
    void colorChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    void rehighlightIfDocumented();

    int m_start = -1;
    int m_length = 0;
    QColor m_color;
};

}  // namespace llocr
