// Rasterizes an SVG icon into PNGs at the given sizes.
//
// Usage:
//   rasterize_icon <in.svg> <outDir> <size> [<size>...]
//
// Requires Qt 6 with the QtSvg (and QtGui/QtCore) frameworks, e.g. compiled as:
//   clang++ -std=c++20 -fPIC \
//     -I<Qt>/include -F<Qt>/lib \
//     -framework QtSvg -framework QtGui -framework QtCore \
//     main.cpp -o rasterize_icon
//
// This is a standalone regeneration helper; it is NOT part of the application
// build. The generated PNGs/ICO/ICNS live under resources/icons.
#include <QImage>
#include <QPainter>
#include <QColor>
#include <QRectF>
#include <QSvgRenderer>
#include <QDir>
#include <QFile>
#include <cstdio>
#include <cstdlib>

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "usage: rasterize_icon <in.svg> <outDir> <size> [<size>...]\n");
        return 2;
    }

    const QString inPath = QString::fromLocal8Bit(argv[1]);
    const QString outDir  = QString::fromLocal8Bit(argv[2]);

    QSvgRenderer renderer(inPath);
    if (!renderer.isValid()) {
        fprintf(stderr, "failed to load SVG: %s\n", qPrintable(inPath));
        return 1;
    }

    const QSize baseSize = renderer.defaultSize();
    if (baseSize.isEmpty()) {
        fprintf(stderr, "SVG has no viewBox / default size\n");
        return 1;
    }

    if (!QDir(outDir).exists() && !QDir().mkpath(outDir)) {
        fprintf(stderr, "cannot create outDir: %s\n", qPrintable(outDir));
        return 1;
    }

    bool ok = true;
    for (int i = 3; i < argc; ++i) {
        const int size = QString::fromLocal8Bit(argv[i]).toInt();
        if (size <= 0) {
            fprintf(stderr, "invalid size: %s\n", argv[i]);
            ok = false;
            continue;
        }
        QImage img(size, size, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.setRenderHint(QPainter::Antialiasing, true);
            renderer.render(&p, QRectF(0, 0, size, size));
            p.end();
        }
        const QString outFile = QDir(outDir).filePath(QStringLiteral("icon-%1.png").arg(size));
        if (!img.save(outFile, "PNG")) {
            fprintf(stderr, "failed to save %s\n", qPrintable(outFile));
            ok = false;
        } else {
            printf("wrote %s\n", qPrintable(outFile));
        }
    }

    return ok ? 0 : 1;
}