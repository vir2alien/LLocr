#include <QtTest>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "app/CandidateRenderer.h"

using namespace llocr;

/**
 * @brief Integration test for the candidate renderer.
 *
 * The visual-diff decision (ADR 235) shows the model two pictures: the crop of
 * the block and the candidate painted back into an image. A candidate that
 * lost its tail, or that arrived framed by white margins, was judged as a
 * different text — 155 of 195 blocks of one corpus were rejected that way
 * (ADR 237). The render leg is therefore checked, not trusted.
 */
class CandidateRendererTest : public QObject
{
    Q_OBJECT

private slots:
    void keepsEveryLineOfALongCandidate();
    void paintsTheCandidateTightToTheInk();
    void paintsAFormulaAsMath();
};

namespace {

void spinUntil(const std::function<bool()> &done, int timeoutMs = 60000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY2(done(), "timed out waiting for the render callback");
}

QImage renderCandidate(const QString &text, bool *ok, QString *error)
{
    CandidateRenderer renderer;
    QImage image;
    bool finished = false;
    renderer.render(text, [&](bool success, const QImage &result, const QString &message) {
        *ok = success;
        *error = message;
        image = result;
        finished = true;
    });
    spinUntil([&]() { return finished; });
    return image;
}

// The rows of the image that carry ink: one band per painted line.
QList<QRect> inkBands(const QImage &image)
{
    QList<QRect> bands;
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    int start = -1;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        bool ink = false;
        for (int x = 0; x < gray.width(); ++x) {
            if (row[x] < 245) {
                ink = true;
                break;
            }
        }
        if (ink && start < 0)
            start = y;
        if (!ink && start >= 0) {
            bands.append(QRect(0, start, gray.width(), y - start));
            start = -1;
        }
    }
    if (start >= 0)
        bands.append(QRect(0, start, gray.width(), gray.height() - start));
    return bands;
}

QString longCandidate(int sentences)
{
    QString text;
    for (int i = 0; i < sentences; ++i)
        text += QStringLiteral("Sentence number %1 carries the words a long paragraph needs. ").arg(i);
    return text.trimmed();
}

// A candidate of exactly `lines` painted lines: the hard break (two trailing
// spaces) keeps every source line on a line of its own.
QString candidateOfLines(int lines)
{
    QStringList parts;
    for (int i = 0; i < lines; ++i)
        parts << QStringLiteral("Line %1 of the candidate.").arg(i + 1);
    return parts.join(QStringLiteral("  \n"));
}

}  // namespace

void CandidateRendererTest::keepsEveryLineOfALongCandidate()
{
    bool ok = false;
    QString error;
    const QImage shorter = renderCandidate(longCandidate(12), &ok, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(!shorter.isNull());

    const QImage longer = renderCandidate(longCandidate(40), &ok, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(!longer.isNull());

    // A candidate with far more text must paint a taller picture. A render
    // that drops the overflow onto a second page cuts both candidates at the
    // same place, and the tail of the longer one never reaches the picture.
    // The jump must clear a whole line: one extra sentence can still fit into
    // the trailing slack of the last line, and the wrap point then depends on
    // the platform's font metrics.
    QVERIFY2(longer.height() > shorter.height(), qPrintable(QStringLiteral("height %1 vs %2").arg(longer.height()).arg(shorter.height())));
    QVERIFY2(inkBands(longer).size() > inkBands(shorter).size(), qPrintable(QStringLiteral("lines %1 vs %2").arg(inkBands(longer).size()).arg(inkBands(shorter).size())));

    // Every line of the candidate must be painted. The page holds the text
    // with room to spare: a line that fell onto a second page would be a line
    // the model never saw.
    const QImage lined = renderCandidate(candidateOfLines(12), &ok, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY2(inkBands(lined).size() == 12, qPrintable(QStringLiteral("painted %1 of 12 lines").arg(inkBands(lined).size())));
}

void CandidateRendererTest::paintsTheCandidateTightToTheInk()
{
    bool ok = false;
    QString error;
    const QImage image = renderCandidate(longCandidate(6), &ok, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(!image.isNull());

    // The crop the model sees next to this picture is tight around the text.
    // A white band around the candidate would make the two pictures look like
    // different regions.
    const QList<QRect> bands = inkBands(image);
    QVERIFY(!bands.isEmpty());
    QVERIFY2(bands.first().top() <= 2, qPrintable(QStringLiteral("top %1").arg(bands.first().top())));
    QVERIFY2(bands.last().bottom() >= image.height() - 3, qPrintable(QStringLiteral("bottom %1 of %2").arg(bands.last().bottom()).arg(image.height())));
}

void CandidateRendererTest::paintsAFormulaAsMath()
{
    bool ok = false;
    QString error;
    // The formula the way the page carries it: display math. Painted as
    // literal LaTeX it would come out as a line of text, and the model would
    // compare a typeset formula with a line of source code.
    const QImage image = renderCandidate(QStringLiteral("$$\n\\frac {1}{2}\n$$"), &ok, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(!image.isNull());

    const QList<QRect> bands = inkBands(image);
    QVERIFY(!bands.isEmpty());
    // A typeset fraction is taller than a line of text and sits in the
    // middle of the picture; the literal LaTeX would be one thin line.
    QVERIFY2(bands.first().height() > 20, qPrintable(QStringLiteral("band height %1").arg(bands.first().height())));
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_MACOS
    // Chromium's multi-process model registers a named Mach rendezvous server
    // in the bootstrap namespace; a restricted session refuses the registration
    // and Chromium aborts with SIGTRAP. The renderer runs in the browser
    // process instead — the assertions stay about the rendered picture.
    const QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags + (flags.isEmpty() ? "" : " ") + "--single-process");
#endif

    // Must run before the QGuiApplication is constructed.
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    CandidateRendererTest tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_candidate_renderer.moc"