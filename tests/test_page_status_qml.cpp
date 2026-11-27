// The page status: the word and the color the page speaks with (ADR 338).
// The mapping lives in QML, so the test reads the shipped file.

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

class TestPageStatusQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void aPageWithoutMarkupHasNoStatus();
    void aMarkedUpPageIsMarkedUp();
    void anUnjudgedPageIsRecognized();
    void aPageWithAnErrorHasErrors();
    void anEditedPageIsEdited();
    void aVerifiedPageIsVerified();
    void theOrientationOutranksEverything();
    void anErrorOutranksTheEdit();
    void theEditWaitsForTheWholePage();
    void theOrientationOutranksTheMarkupAndTheError();

private:
    QString statusText(int mode, int verification, bool edited, int orientation);
    QColor statusColor(int mode, int verification, bool edited, int orientation);
    QColor themeColor(const QString &name);

    std::unique_ptr<FakeUiController> m_uiController;
    QString m_qmlRoot;
    std::unique_ptr<QQmlEngine> m_engine;
    QObject *m_status = nullptr;
    QObject *m_theme = nullptr;
    QStringList m_warnings;
};

void TestPageStatusQml::initTestCase()
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
    registerFile(m_qmlRoot + QStringLiteral("/PageStatus.qml"), "PageStatus");

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    m_status = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("PageStatus"));
    QVERIFY(m_status);
    m_theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(m_theme);
}

// The mapping must load and run without a word of complaint: a broken branch
// the test happens not to call would otherwise stay unnoticed.
void TestPageStatusQml::cleanup()
{
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("; "))));
}

QString TestPageStatusQml::statusText(int mode, int verification, bool edited, int orientation)
{
    QVariant result;
    const bool ok =
        QMetaObject::invokeMethod(m_status, "statusText", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, mode), Q_ARG(QVariant, verification), Q_ARG(QVariant, edited), Q_ARG(QVariant, orientation));
    if (!QTest::qVerify(ok, "statusText()", "the page status has a statusText()", __FILE__, __LINE__))
        return QString();
    return result.toString();
}

QColor TestPageStatusQml::statusColor(int mode, int verification, bool edited, int orientation)
{
    QVariant result;
    const bool ok =
        QMetaObject::invokeMethod(m_status, "statusColor", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, mode), Q_ARG(QVariant, verification), Q_ARG(QVariant, edited), Q_ARG(QVariant, orientation));
    if (!QTest::qVerify(ok, "statusColor()", "the page status has a statusColor()", __FILE__, __LINE__))
        return QColor();
    return result.value<QColor>();
}

QColor TestPageStatusQml::themeColor(const QString &name)
{
    return m_theme->property(name.toUtf8().constData()).value<QColor>();
}

// A page without a markup block has nothing to say.
void TestPageStatusQml::aPageWithoutMarkupHasNoStatus()
{
    QCOMPARE(statusText(0, 0, false, 0), QString());
    QCOMPARE(statusText(2, 0, false, 0), QString());

    // The color of the state with no status is transparent: the dot of the
    // thumbnail draws nothing.
    const QColor color = statusColor(0, 0, false, 0);
    QVERIFY(color.isValid());
    QCOMPARE(color.alpha(), 0);
}

// The markup alone: the page says it is marked up.
void TestPageStatusQml::aMarkedUpPageIsMarkedUp()
{
    QCOMPARE(statusText(1, 0, false, 0), QStringLiteral("Marked up"));
    QCOMPARE(statusColor(1, 0, false, 0), themeColor(QStringLiteral("nothing")));
}

// Every block is recognized, one of them is not judged yet.
void TestPageStatusQml::anUnjudgedPageIsRecognized()
{
    QCOMPARE(statusText(3, 0, false, 0), QStringLiteral("Recognized"));
    QCOMPARE(statusColor(3, 0, false, 0), themeColor(QStringLiteral("nothing")));
}

// Every block is judged and one of them has an error.
void TestPageStatusQml::aPageWithAnErrorHasErrors()
{
    QCOMPARE(statusText(3, 1, false, 0), QStringLiteral("Errors"));
    QCOMPARE(statusColor(3, 1, false, 0), themeColor(QStringLiteral("error")));
}

// Every block is judged and one of them was written by hand.
void TestPageStatusQml::anEditedPageIsEdited()
{
    QCOMPARE(statusText(3, 2, true, 0), QStringLiteral("Edited"));
    QCOMPARE(statusColor(3, 2, true, 0), themeColor(QStringLiteral("warning")));
}

// Every block is judged and none of them has an error.
void TestPageStatusQml::aVerifiedPageIsVerified()
{
    QCOMPARE(statusText(3, 2, false, 0), QStringLiteral("Verified"));
    QCOMPARE(statusColor(3, 2, false, 0), themeColor(QStringLiteral("success")));
}

// The page lies on its side: it needs a turn before anything else.
void TestPageStatusQml::theOrientationOutranksEverything()
{
    QCOMPARE(statusText(3, 2, false, 1), QStringLiteral("On its side"));
    QCOMPARE(statusColor(3, 2, false, 1), themeColor(QStringLiteral("error")));
    QCOMPARE(statusText(3, 2, false, 2), QStringLiteral("Orientation unclear"));
}

// The error comes first: the page needs a human eye before the hand-written
// text of the user matters.
void TestPageStatusQml::anErrorOutranksTheEdit()
{
    QCOMPARE(statusText(3, 1, true, 0), QStringLiteral("Errors"));
    QCOMPARE(statusColor(3, 1, true, 0), themeColor(QStringLiteral("error")));
}

// The hand-written text counts only when the whole page is judged: until
// then the page says that it is recognized.
void TestPageStatusQml::theEditWaitsForTheWholePage()
{
    QCOMPARE(statusText(3, 0, true, 0), QStringLiteral("Recognized"));
    QCOMPARE(statusColor(3, 0, true, 0), themeColor(QStringLiteral("nothing")));
}

// The turn the page needs comes before the markup and before the error.
void TestPageStatusQml::theOrientationOutranksTheMarkupAndTheError()
{
    QCOMPARE(statusText(1, 0, false, 1), QStringLiteral("On its side"));
    QCOMPARE(statusColor(1, 0, false, 1), themeColor(QStringLiteral("error")));
    QCOMPARE(statusText(3, 1, true, 1), QStringLiteral("On its side"));
}

QTEST_MAIN(TestPageStatusQml)
#include "test_page_status_qml.moc"
