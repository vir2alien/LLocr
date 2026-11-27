// The frame of a block in the preview: the color it wears and whether it wears
// one at all (ADR 238, ADR 338). The mapping lives in QML, so the test reads
// the shipped file.

#include <memory>
#include <QDir>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QtTest>

namespace {

// A stand-in for the UiController: Theme reads the dark flag and the language
// from it.
class FakeUiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark CONSTANT)
    Q_PROPERTY(QString language READ language CONSTANT)

public:
    bool dark() const { return false; }
    QString language() const { return QStringLiteral("en"); }
};

}  // namespace

class TestBlockFrameQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void theMarkupAloneHasNoFrame();
    void aRecognizedBlockIsGray();
    void aVerifiedBlockIsGreen();
    void aBlockWithAnErrorIsRed();
    void aHandWrittenBlockIsYellow();
    void theHandWrittenTextOutranksTheVerdict();
    void theReaderConfirmsTheBlockInGreen();
    void theReaderMissesTheBlockInRed();
    void anUnreadableBlockStillHasAFrame();

private:
    QColor frameColor(int effectiveStatus, int fixStatus, bool documentDiverged, bool hasText);
    QColor themeColor(const QString &name);

    std::unique_ptr<FakeUiController> m_uiController;
    QString m_qmlRoot;
    std::unique_ptr<QQmlEngine> m_engine;
    QObject *m_frame = nullptr;
    QObject *m_theme = nullptr;
    QStringList m_warnings;
};

void TestBlockFrameQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    m_uiController = std::make_unique<FakeUiController>();
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    const auto registerFile = [](const QString &path, const char *name) {
        const QUrl url = QUrl::fromLocalFile(path);
        qmlRegisterSingletonType(url, "LLocr", 1, 0, name);
    };
    registerFile(m_qmlRoot + QStringLiteral("/Theme.qml"), "Theme");
    registerFile(m_qmlRoot + QStringLiteral("/BlockFrame.qml"), "BlockFrame");

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    m_frame = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("BlockFrame"));
    QVERIFY(m_frame);
    m_theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(m_theme);
}

// The mapping must load and run without a word of complaint: a broken branch
// the test happens not to call would otherwise stay unnoticed.
void TestBlockFrameQml::cleanup()
{
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));
}

QColor TestBlockFrameQml::frameColor(int effectiveStatus, int fixStatus, bool documentDiverged, bool hasText)
{
    QVariant result;
    const bool ok = QMetaObject::invokeMethod(
        m_frame, "frameColor", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, effectiveStatus), Q_ARG(QVariant, fixStatus), Q_ARG(QVariant, documentDiverged), Q_ARG(QVariant, hasText));
    if (!QTest::qVerify(ok, "frameColor()", "the block frame has a frameColor()", __FILE__, __LINE__))
        return QColor();
    return result.value<QColor>();
}

QColor TestBlockFrameQml::themeColor(const QString &name)
{
    return m_theme->property(name.toUtf8().constData()).value<QColor>();
}

// The markup alone is not a block yet: it carries no text and no frame.
void TestBlockFrameQml::theMarkupAloneHasNoFrame()
{
    const QColor color = frameColor(0, 0, false, false);
    QVERIFY(color.isValid());
    QCOMPARE(color.alpha(), 0);
}

// A block with text that nobody judged is gray.
void TestBlockFrameQml::aRecognizedBlockIsGray()
{
    QCOMPARE(frameColor(0, 0, false, true), themeColor(QStringLiteral("nothing")));
}

// The decision model confirmed the text: green.
void TestBlockFrameQml::aVerifiedBlockIsGreen()
{
    QCOMPARE(frameColor(1, 0, false, true), themeColor(QStringLiteral("success")));
    // The user approved the block by hand: green as well.
    QCOMPARE(frameColor(5, 0, false, true), themeColor(QStringLiteral("success")));
}

// The decision model rejected the text: red.
void TestBlockFrameQml::aBlockWithAnErrorIsRed()
{
    QCOMPARE(frameColor(4, 0, false, true), themeColor(QStringLiteral("error")));
    // The block could not be read: red.
    QCOMPARE(frameColor(3, 0, false, true), themeColor(QStringLiteral("error")));
    // The formula's LaTeX is broken: red.
    QCOMPARE(frameColor(7, 0, false, true), themeColor(QStringLiteral("error")));
}

// The block the user wrote by hand is yellow — the same yellow the header of
// the block carries (ADR 338).
void TestBlockFrameQml::aHandWrittenBlockIsYellow()
{
    // The user wrote the text of the layer itself.
    QCOMPARE(frameColor(6, 0, false, true), themeColor(QStringLiteral("warning")));
    // The document keeps the user's own wording (ADR 296).
    QCOMPARE(frameColor(4, 0, true, true), themeColor(QStringLiteral("warning")));
}

// The hand-written text outranks the verdict of the model: the frame says what
// the block is now.
void TestBlockFrameQml::theHandWrittenTextOutranksTheVerdict()
{
    QCOMPARE(frameColor(1, 0, true, true), themeColor(QStringLiteral("warning")));
    QCOMPARE(frameColor(4, 0, true, true), themeColor(QStringLiteral("warning")));
}

// The reader confirmed the text: the block is verified (ADR 261).
void TestBlockFrameQml::theReaderConfirmsTheBlockInGreen()
{
    QCOMPARE(frameColor(4, 4, false, true), themeColor(QStringLiteral("success")));
}

// The reader could not read the block: red.
void TestBlockFrameQml::theReaderMissesTheBlockInRed()
{
    QCOMPARE(frameColor(0, 6, false, true), themeColor(QStringLiteral("error")));
}

// A block the reader could not read carries a verdict without text: the frame
// is the only sign that the block needs a human (ADR 261).
void TestBlockFrameQml::anUnreadableBlockStillHasAFrame()
{
    const QColor color = frameColor(0, 6, false, false);
    QVERIFY(color.isValid());
    QCOMPARE(color.alpha(), 255);
    QCOMPARE(color, themeColor(QStringLiteral("error")));
}

QTEST_MAIN(TestBlockFrameQml)
#include "test_block_frame_qml.moc"
