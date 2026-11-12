// The run indicator takes the icon's place, not the caption's: a button that
// started a run keeps naming the action while the ring spins (ADR 305). The
// test loads the shipped button from the source tree, turns the run on and
// measures: the caption stays visible, the glyph gives way to the ring, and
// the button keeps its width — the toolbar must not jump when a run starts.

#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>

#include "app/UiController.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

class TestIconToolButtonQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void theCaptionSurvivesTheRunIndicator();
    void theButtonKeepsItsWidthWhenTheRunStarts();
    void theStopSignSitsInTheRingCenter();

private:
    std::unique_ptr<QObject> loadButton();
    QObject *findByClassName(QObject *root, const QLatin1String &className);
    QObject *iconNamed(QObject *root, const QString &glyph);
    double effectiveOpacityOf(QObject *item);

    QString m_qmlRoot;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QStringList m_warnings;
};

void TestIconToolButtonQml::initTestCase()
{
    // The app runs the Fusion style, and the native macOS style warns about
    // every customized control.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    m_settings = std::make_unique<SettingsStore>();
    m_uiController = std::make_unique<UiController>(*m_settings);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    // The theme the button paints itself with is loaded from the source tree,
    // under the module the app uses.
    qmlRegisterSingletonType(QUrl::fromLocalFile(m_qmlRoot + QStringLiteral("/Theme.qml")), "LLocr", 1, 0, "Theme");

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported) {
            m_warnings.append(error.toString());
        }
    });

    m_window = std::make_unique<QQuickWindow>();
    m_window->resize(400, 200);
    m_window->show();
}

std::unique_ptr<QObject> TestIconToolButtonQml::loadButton()
{
    m_warnings.clear();

    const QString path = QDir(m_qmlRoot).filePath(QStringLiteral("Common/IconToolButton.qml"));
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    if (!component.isReady()) {
        m_warnings.append(component.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        m_warnings.append(component.errorString());
        return nullptr;
    }
    object->setProperty("text", QStringLiteral("Mark up"));
    object->setProperty("iconName", QStringLiteral("layout-template"));
    if (auto *item = qobject_cast<QQuickItem *>(object.get())) {
        item->setParentItem(m_window->contentItem());
    }
    // The layout is applied in the polish: let it happen before the test
    // measures the button.
    QTest::qWait(20);
    return object;
}

QObject *TestIconToolButtonQml::findByClassName(QObject *root, const QLatin1String &className)
{
    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        // A QML type's class name carries the engine's suffix
        // ("Icon_QMLTYPE_23"), so the name is matched by its prefix.
        const QString name = QString::fromLatin1(child->metaObject()->className());
        if (name == className || name.startsWith(className + QStringLiteral("_QMLTYPE"))) {
            return child;
        }
        if (QObject *found = findByClassName(child, className)) {
            return found;
        }
    }
    return nullptr;
}

// The glyphs are told apart by the name they draw — the button's own icon and
// the ring of the run indicator.
QObject *TestIconToolButtonQml::iconNamed(QObject *root, const QString &glyph)
{
    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        const QVariant name = child->property("name");
        if (name.isValid() && name.toString() == glyph) {
            return child;
        }
        if (QObject *found = iconNamed(child, glyph)) {
            return found;
        }
    }
    return nullptr;
}

// The opacity of an item and of all its ancestors: a caption that is faded
// out is gone, whatever its own `visible` says.
double TestIconToolButtonQml::effectiveOpacityOf(QObject *item)
{
    double opacity = 1.0;
    QObject *node = item;
    while (node) {
        auto *quick = qobject_cast<QQuickItem *>(node);
        if (!quick) {
            break;
        }
        opacity *= quick->opacity();
        node = quick->parentItem();
    }
    return opacity;
}

void TestIconToolButtonQml::theCaptionSurvivesTheRunIndicator()
{
    const std::unique_ptr<QObject> button = loadButton();
    QVERIFY2(button, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *caption = findByClassName(button.get(), QLatin1String("QQuickText"));
    QVERIFY2(caption, "the button must carry a caption");
    QCOMPARE(caption->property("text").toString(), QStringLiteral("Mark up"));

    QObject *glyph = iconNamed(button.get(), QStringLiteral("layout-template"));
    QVERIFY2(glyph, "the button must carry a glyph");
    QObject *ring = iconNamed(button.get(), QStringLiteral("loader-circle"));
    QVERIFY2(ring, "the run indicator must be a ring");

    auto *glyphItem = qobject_cast<QQuickItem *>(glyph);
    auto *ringItem = qobject_cast<QQuickItem *>(ring);
    QVERIFY(glyphItem && ringItem);

    QVERIFY(caption->property("visible").toBool());
    QVERIFY(glyphItem->isVisible());
    QVERIFY(!ringItem->isVisible());

    button->setProperty("stopMode", true);
    QCoreApplication::processEvents();

    // The caption stays: the button still names the action it started.
    QCOMPARE(caption->property("text").toString(), QStringLiteral("Mark up"));
    QVERIFY(caption->property("visible").toBool());
    QVERIFY2(qobject_cast<QQuickItem *>(caption)->isVisible(), "the caption must not be hidden");
    QVERIFY2(effectiveOpacityOf(caption) > 0.5, "the caption must not be faded out");
    // The glyph gives way to the indicator — and to nothing else.
    QVERIFY(!glyphItem->isVisible());
    QVERIFY(ringItem->isVisible());
}

void TestIconToolButtonQml::theButtonKeepsItsWidthWhenTheRunStarts()
{
    const std::unique_ptr<QObject> button = loadButton();
    QVERIFY2(button, qPrintable(m_warnings.join(QStringLiteral("; "))));

    const qreal idleWidth = button->property("implicitWidth").toReal();
    QVERIFY(idleWidth > 0);

    button->setProperty("stopMode", true);
    QCoreApplication::processEvents();

    QCOMPARE(button->property("implicitWidth").toReal(), idleWidth);
}

void TestIconToolButtonQml::theStopSignSitsInTheRingCenter()
{
    const std::unique_ptr<QObject> button = loadButton();
    QVERIFY2(button, qPrintable(m_warnings.join(QStringLiteral("; "))));
    button->setProperty("stopMode", true);
    QCoreApplication::processEvents();

    QObject *ring = iconNamed(button.get(), QStringLiteral("loader-circle"));
    QObject *sign = iconNamed(button.get(), QStringLiteral("square"));
    QObject *glyph = iconNamed(button.get(), QStringLiteral("layout-template"));
    QVERIFY2(ring, "the run indicator must be a ring");
    QVERIFY2(sign, "the run indicator must carry the stop sign");
    QVERIFY2(glyph, "the button must carry a glyph");

    auto *ringItem = qobject_cast<QQuickItem *>(ring);
    auto *signItem = qobject_cast<QQuickItem *>(sign);
    auto *glyphItem = qobject_cast<QQuickItem *>(glyph);
    QVERIFY(ringItem && signItem && glyphItem);

    // The sign and the ring are drawn by the same component: their boxes
    // share one center and their sizes are even, so both glyphs land on
    // whole pixels and the sign sits in the middle of the ring.
    QCOMPARE(signItem->width(), signItem->height());
    QVERIFY(qint32(signItem->width()) % 2 == 0);
    QVERIFY2(signItem->width() * 8 <= ringItem->width() * 5, "the sign must fit inside the ring");
    QCOMPARE(signItem->x() + signItem->width() / 2.0, ringItem->x() + ringItem->width() / 2.0);
    QCOMPARE(signItem->y() + signItem->height() / 2.0, ringItem->y() + ringItem->height() / 2.0);

    // The ring is the icon's place: it is bigger than the glyph it replaces,
    // and it keeps that glyph's center, so it grows symmetrically.
    QVERIFY2(ringItem->width() > glyphItem->width(), "the run indicator must be bigger than the glyph");
    QCOMPARE(ringItem->x() + ringItem->width() / 2.0, glyphItem->x() + glyphItem->width() / 2.0);
    QCOMPARE(ringItem->y() + ringItem->height() / 2.0, glyphItem->y() + glyphItem->height() / 2.0);

    // The sign is the shipped glyph, not a filled block: a rectangle would
    // be drawn on whole pixels and would sit half a pixel off the ring.
    QVERIFY2(!findByClassName(ringItem->parentItem(), QLatin1String("QQuickRectangle")), "the stop sign must not be a filled block");
}

QTEST_MAIN(TestIconToolButtonQml)
#include "test_icon_tool_button_qml.moc"
