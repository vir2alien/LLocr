// The app's one switch: `Common/SegmentedControl.qml`.
//
// The switch is driven from the outside: it reports the choice and never
// writes the index itself. Seven places use it, and each of them binds the
// index to a stored value — the theme, the connection mode, the orientation,
// the thinking level, the Markdown preview. A click that wrote the index
// would break that binding: the switch would stop following the setting, and
// «Reset» in the verification settings would leave the switch showing the
// old level.
//
// The test loads the shipped QML from the source tree and clicks the real
// segments — the switch is the thing the user touches, so the test touches
// it too.

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

#include "app/UiController.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

class TestSegmentedControlQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void theSwitchReportsTheChoiceWithoutWritingTheIndex();
    void theBindingToTheStoredValueSurvivesAClick();
    void anOptionMayCarryAnIcon();

private:
    std::unique_ptr<QObject> loadFile(const QString &relativePath);
    std::unique_ptr<QObject> loadInline(const QString &qml);
    QQuickItem *segmentAt(QObject *control, int index);
    void clickSegment(QObject *control, int index);
    QObject *findByClassName(QObject *root, const QLatin1String &className);

    QString m_qmlRoot;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_parent = nullptr;
    QStringList m_warnings;
};

void TestSegmentedControlQml::initTestCase()
{
    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    m_settings = std::make_unique<SettingsStore>();
    m_uiController = std::make_unique<UiController>(*m_settings);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    // The switch, its icon and the theme it paints itself with are loaded
    // from the source tree, under the module the app uses.
    const auto registerFile = [](const QString &path, const char *name, bool singleton) {
        const QUrl url = QUrl::fromLocalFile(path);
        if (singleton)
            qmlRegisterSingletonType(url, "LLocr", 1, 0, name);
        else
            qmlRegisterType(url, "LLocr", 1, 0, name);
    };
    registerFile(m_qmlRoot + QStringLiteral("/Theme.qml"), "Theme", true);
    registerFile(m_qmlRoot + QStringLiteral("/Common/Icon.qml"), "Icon", false);
    registerFile(m_qmlRoot + QStringLiteral("/Common/SegmentedControl.qml"), "SegmentedControl", false);

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    m_window = std::make_unique<QQuickWindow>();
    m_parent = new QQuickItem(m_window->contentItem());
    m_parent->setSize(QSizeF(800, 600));
    m_window->resize(800, 600);
    m_window->show();
}

std::unique_ptr<QObject> TestSegmentedControlQml::loadFile(const QString &relativePath)
{
    m_warnings.clear();

    const QString path = QDir(m_qmlRoot).filePath(relativePath);
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
    if (auto *item = qobject_cast<QQuickItem *>(object.get())) {
        item->setParentItem(m_parent);
        item->setWidth(400);
        item->setHeight(30);
    }
    return object;
}

std::unique_ptr<QObject> TestSegmentedControlQml::loadInline(const QString &qml)
{
    m_warnings.clear();

    QQmlComponent component(m_engine.get());
    component.setData(qml.toUtf8(), QUrl(QStringLiteral("qrc:/inline/SegmentedSwitch.qml")));
    if (!component.isReady()) {
        m_warnings.append(component.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        m_warnings.append(component.errorString());
        return nullptr;
    }
    if (auto *item = qobject_cast<QQuickItem *>(object.get())) {
        item->setParentItem(m_parent);
        item->setWidth(400);
        item->setHeight(30);
    }
    return object;
}

QObject *TestSegmentedControlQml::findByClassName(QObject *root, const QLatin1String &className)
{
    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        // A QML type's class name carries the engine's suffix
        // ("Icon_QMLTYPE_23"), so the name is matched by its prefix.
        const QString name = QString::fromLatin1(child->metaObject()->className());
        if (name == className || name.startsWith(className + QStringLiteral("_QMLTYPE")))
            return child;
        if (QObject *found = findByClassName(child, className))
            return found;
    }
    return nullptr;
}

// The segments are the Repeater's delegates: they come in model order.
QQuickItem *TestSegmentedControlQml::segmentAt(QObject *control, int index)
{
    QObject *repeater = findByClassName(control, QLatin1String("QQuickRepeater"));
    if (!repeater)
        return nullptr;

    QQmlContext *context = m_engine->contextForObject(repeater);
    if (!context)
        return nullptr;

    QQmlExpression expression(context, repeater, QStringLiteral("itemAt(%1)").arg(index));
    const QVariant result = expression.evaluate();
    if (expression.hasError()) {
        m_warnings.append(expression.error().toString());
        return nullptr;
    }
    return qobject_cast<QQuickItem *>(result.value<QObject *>());
}

void TestSegmentedControlQml::clickSegment(QObject *control, int index)
{
    QQuickItem *segment = segmentAt(control, index);
    QVERIFY2(segment, "the switch must have a segment to click");
    const QPointF center(segment->width() / 2, segment->height() / 2);
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, segment->mapToScene(center).toPoint());
    QCoreApplication::processEvents();
}

// A click reports the choice — and the switch does not write the index
// itself: the owner of the switch decides what the index means.
void TestSegmentedControlQml::theSwitchReportsTheChoiceWithoutWritingTheIndex()
{
    const std::unique_ptr<QObject> control = loadFile(QStringLiteral("Common/SegmentedControl.qml"));
    QVERIFY2(control, qPrintable(m_warnings.join(QStringLiteral("; "))));

    control->setProperty("options", QVariantList{QVariantMap{{QStringLiteral("label"), QStringLiteral("Text (MD)")}}, QVariantMap{{QStringLiteral("label"), QStringLiteral("Preview")}}});
    QCoreApplication::processEvents();

    QSignalSpy spy(control.get(), SIGNAL(activated(int)));
    clickSegment(control.get(), 1);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
    QCOMPARE(control->property("currentIndex").toInt(), 0);
}

// The seven places that use the switch bind the index to a stored value. The
// binding must survive the click: the setting writes the value, the binding
// brings the switch back — and a change of the setting from anywhere else
// still moves the switch.
void TestSegmentedControlQml::theBindingToTheStoredValueSurvivesAClick()
{
    const std::unique_ptr<QObject> control = loadInline(QStringLiteral(R"(
import QtQuick
import LLocr

SegmentedControl {
    id: sw
    property int stored: 0
    options: [{ label: "A" }, { label: "B" }]
    currentIndex: sw.stored
    onActivated: (index) => sw.stored = index
}
)"));
    QVERIFY2(control, qPrintable(m_warnings.join(QStringLiteral("; "))));

    // The stored value moves the switch.
    control->setProperty("stored", 1);
    QCoreApplication::processEvents();
    QCOMPARE(control->property("currentIndex").toInt(), 1);

    // The click writes the stored value and the switch follows it.
    QSignalSpy spy(control.get(), SIGNAL(activated(int)));
    clickSegment(control.get(), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);
    QCOMPARE(control->property("stored").toInt(), 0);
    QCOMPARE(control->property("currentIndex").toInt(), 0);

    // The binding is still alive: a change of the stored value from anywhere
    // else still moves the switch.
    control->setProperty("stored", 1);
    QCoreApplication::processEvents();
    QCOMPARE(control->property("currentIndex").toInt(), 1);
}

// An option may carry an icon — the theme picker keeps its monitor, sun and
// moon. The icon takes its place next to the label, so the segment grows.
void TestSegmentedControlQml::anOptionMayCarryAnIcon()
{
    const std::unique_ptr<QObject> control = loadFile(QStringLiteral("Common/SegmentedControl.qml"));
    QVERIFY2(control, qPrintable(m_warnings.join(QStringLiteral("; "))));

    control->setProperty("options",
                         QVariantList{QVariantMap{{QStringLiteral("label"), QStringLiteral("Same")}, {QStringLiteral("icon"), QStringLiteral("monitor")}},
                                      QVariantMap{{QStringLiteral("label"), QStringLiteral("Same")}}});
    QCoreApplication::processEvents();

    QQuickItem *withIcon = segmentAt(control.get(), 0);
    QQuickItem *withoutIcon = segmentAt(control.get(), 1);
    QVERIFY2(withIcon, "the switch must have a segment with an icon");
    QVERIFY2(withoutIcon, "the switch must have a segment without an icon");
    QVERIFY(withIcon->width() > withoutIcon->width());

    QVERIFY(findByClassName(withIcon, QLatin1String("Icon")));
}

QTEST_MAIN(TestSegmentedControlQml)
#include "test_segmented_control_qml.moc"
