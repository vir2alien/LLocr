#pragma once

#include <QImage>
#include <QPainter>

// A page of text-like lines. Every letter carries the body of an x-height
// letter, some of them an ascender reaching above it and some a descender
// hanging below it — the shapes the orientation check reads.
//
// The letters touch each other, the way the strokes of a printed line do: a
// page whose letters stand apart has gaps between them that a page of real
// text does not, and the check reads those gaps as lines of their own.
inline QImage syntheticTextPage(int width = 900, int height = 1200)
{
    QImage page(width, height, QImage::Format_Grayscale8);
    page.fill(255);
    QPainter painter(&page);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0));

    const int margin = width / 12;
    const int textWidth = width - 2 * margin;
    const int lines = 24;
    const int lineStep = height / (lines + 2);
    const int body = lineStep / 3;       // the x-height band
    const int ascender = lineStep / 6;   // above the band
    const int descender = lineStep / 6;  // below it
    const int letterStep = 5;            // the width of a letter and its pitch

    for (int line = 0; line < lines; ++line) {
        const int baseline = (line + 1) * lineStep;
        const int bodyTop = baseline - body;
        int x = margin;
        while (x < margin + textWidth) {
            const int wordLength = 30 + (x * 7) % 70;
            for (int letter = 0; letter * letterStep < wordLength; ++letter) {
                const int lx = x + letter * letterStep;
                if (lx + letterStep >= margin + textWidth)
                    break;
                const int kind = (lx / letterStep + line * 3) % 20;
                painter.drawRect(QRect(lx, bodyTop, letterStep, body));
                if (kind < 4)
                    painter.drawRect(QRect(lx + 1, bodyTop - ascender, 1, ascender));
                else if (kind < 6)
                    painter.drawRect(QRect(lx + 1, baseline, 1, descender));
                else if (kind < 8) {
                    painter.drawRect(QRect(lx + 1, bodyTop - ascender, 1, ascender));
                    painter.drawRect(QRect(lx + 1, baseline, 1, descender));
                } else if (kind < 10) {
                    painter.drawRect(QRect(lx + 1, bodyTop - ascender, 1, ascender));
                }
            }
            x += wordLength + 12;
        }
    }
    painter.end();
    return page;
}
