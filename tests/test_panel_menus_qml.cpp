// The three panel menus — the thumbnails, the page and the preview — stand side
// by side, so they share one height and their bottom edges line up (ADR 301).
// The test loads the shipped QML from the source tree and measures the three
// bands: they must be equal, and the «Select all» button of the thumbnail menu
// must be the app's icon-and-caption button, standing centered in its band.
//
// The panels read the document's state from the controller singleton; the test
// registers a double (tests/fake_controller.qml) that reports a document with
// three pages and an idle app.

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>

#include "app/EditorBridge.h"
#include "app/UiController.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

class TestPanelMenusQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void theThreeMenusShareOneHeight();
    void theSelectAllButtonIsTheAppsButton();
    void theZoomStandsInTheToolbar();
    void theSwitchOffersThePreview();

private:
    std::unique_ptr<QObject> loadFile(const QString &relativePath, qreal width, qreal height);
    std::unique_ptr<QObject> loadInline(const QString &qml, qreal width, qreal height);
    QObject *findByClassName(QObject *root, const QLatin1String &className);
    QObject *findByProperty(QObject *root, const char *name, const QVariant &value);
    QObject *findText(QObject *root, const QString &text);
    int countMenus(QObject *root);
    QQuickItem *bandOf(QObject *panel);
    QQuickItem *selectAllButtonOf(QObject *thumbPanel);
    double zoomOf(QObject *toolBar);

    QString m_qmlRoot;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_parent = nullptr;
    QStringList m_warnings;
};

void TestPanelMenusQml::initTestCase()
{
    // The app runs the Fusion style, and the native macOS style warns about
    // every customized control.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    const QString fake = QFINDTESTDATA(QStringLiteral("fake_controller.qml"));
    QVERIFY(!fake.isEmpty());

    m_settings = std::make_unique<SettingsStore>();
    m_uiController = std::make_unique<UiController>(*m_settings);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());
    // The preview panel holds the markdown editor, which talks over the web
    // bridge — the type must exist for the panel to load at all.
    qmlRegisterType<llocr::EditorBridge>("LLocr", 1, 0, "EditorBridge");

    // The theme, the page status and the controller double are loaded from
    // the source tree, under the module the app uses.
    const auto registerFile = [](const QString &path, const char *name, bool singleton) {
        const QUrl url = QUrl::fromLocalFile(path);
        if (singleton)
            qmlRegisterSingletonType(url, "LLocr", 1, 0, name);
        else
            qmlRegisterType(url, "LLocr", 1, 0, name);
    };
    registerFile(m_qmlRoot + QStringLiteral("/Theme.qml"), "Theme", true);
    registerFile(m_qmlRoot + QStringLiteral("/PageStatus.qml"), "PageStatus", true);
    registerFile(m_qmlRoot + QStringLiteral("/BlockNames.qml"), "BlockNames", true);
    registerFile(fake, "Controller", true);
    registerFile(m_qmlRoot + QStringLiteral("/MainWindow/PageToolBar.qml"), "PageToolBar", false);

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

std::unique_ptr<QObject> TestPanelMenusQml::loadFile(const QString &relativePath, qreal width, qreal height)
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
        item->setWidth(width);
        item->setHeight(height);
    }
    // The layout is applied in the polish: let it happen before the test
    // measures a band.
    QTest::qWait(20);
    return object;
}

std::unique_ptr<QObject> TestPanelMenusQml::loadInline(const QString &qml, qreal width, qreal height)
{
    m_warnings.clear();

    QQmlComponent component(m_engine.get());
    component.setData(qml.toUtf8(), QUrl(QStringLiteral("qrc:/inline/PanelMenus.qml")));
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
        item->setWidth(width);
        item->setHeight(height);
    }
    QTest::qWait(20);
    return object;
}

QObject *TestPanelMenusQml::findByClassName(QObject *root, const QLatin1String &className)
{
    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        // A QML type's class name carries the engine's suffix
        // ("IconToolButton_QMLTYPE_23"), so the name is matched by its prefix.
        const QString name = QString::fromLatin1(child->metaObject()->className());
        if (name == className || name.startsWith(className + QStringLiteral("_QMLTYPE")))
            return child;
        if (QObject *found = findByClassName(child, className))
            return found;
    }
    return nullptr;
}

// The zoom step carries the name of its glyph — the property tells the minus
// from the plus without a look at the picture.
QObject *TestPanelMenusQml::findByProperty(QObject *root, const char *name, const QVariant &value)
{
    for (QObject *child : root->children()) {
        if (child->metaObject()->indexOfProperty(name) >= 0 && child->property(name) == value)
            return child;
        if (QObject *found = findByProperty(child, name, value))
            return found;
    }
    return nullptr;
}

QObject *TestPanelMenusQml::findText(QObject *root, const QString &text)
{
    for (QObject *child : root->children()) {
        if (child->inherits("QQuickText") && child->property("text").toString() == text)
            return child;
        if (QObject *found = findText(child, text))
            return found;
    }
    return nullptr;
}

int TestPanelMenusQml::countMenus(QObject *root)
{
    int count = 0;
    for (QObject *child : root->children()) {
        if (child->inherits("QQuickMenu"))
            ++count;
        count += countMenus(child);
    }
    return count;
}

// The zoom of the preview the toolbar acts on.
double TestPanelMenusQml::zoomOf(QObject *toolBar)
{
    QObject *preview = toolBar->property("preview").value<QObject *>();
    return preview ? preview->property("zoom").toDouble() : 0.0;
}

// The menu of a panel is its first row: the header that carries the panel's
// tools. The page panel's menu is the toolbar itself — it is loaded inside a
// column, the way the image panel holds it.
QQuickItem *TestPanelMenusQml::bandOf(QObject *panel)
{
    return qobject_cast<QQuickItem *>(findByClassName(panel, QLatin1String("QQuickRowLayout")));
}

QQuickItem *TestPanelMenusQml::selectAllButtonOf(QObject *thumbPanel)
{
    QObject *button = findByClassName(thumbPanel, QLatin1String("IconToolButton"));
    return qobject_cast<QQuickItem *>(button);
}

// The three menus stand in one row of the window: their heights must be equal,
// or their bottom edges do not line up.
void TestPanelMenusQml::theThreeMenusShareOneHeight()
{
    const std::unique_ptr<QObject> thumb = loadFile(QStringLiteral("MainWindow/ThumbPanel.qml"), 200, 600);
    QVERIFY2(thumb, qPrintable(m_warnings.join(QStringLiteral("; "))));
    const std::unique_ptr<QObject> preview = loadFile(QStringLiteral("MainWindow/WorkPanel.qml"), 400, 600);
    QVERIFY2(preview, qPrintable(m_warnings.join(QStringLiteral("; "))));
    // The page menu is the toolbar of the image panel: it is measured inside
    // the column that holds it, so the number is the laid-out height.
    const std::unique_ptr<QObject> page = loadInline(QStringLiteral(R"(
import QtQuick
import QtQuick.Layouts
import LLocr

ColumnLayout {
    PageToolBar {
        id: bar
        Layout.fillWidth: true
    }
}
)"),
                                                     400,
                                                     600);
    QVERIFY2(page, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QQuickItem *thumbBand = bandOf(thumb.get());
    QQuickItem *previewBand = bandOf(preview.get());
    QQuickItem *pageBand = qobject_cast<QQuickItem *>(findByClassName(page.get(), QLatin1String("PageToolBar")));
    QVERIFY2(thumbBand, "the thumbnail panel must have a menu row");
    QVERIFY2(previewBand, "the preview panel must have a menu row");
    QVERIFY2(pageBand, "the page panel must have a menu toolbar");

    QVERIFY2(thumbBand->height() > 0, "the thumbnail menu must be laid out");
    QCOMPARE(thumbBand->height(), pageBand->height());
    QCOMPARE(thumbBand->height(), previewBand->height());

    // The shared height is the theme's one — the menus do not keep their own.
    const std::unique_ptr<QObject> theme = loadInline(QStringLiteral(R"(
import QtQuick
import LLocr

QtObject {
    property int menuHeight: Theme.panelMenuHeight
}
)"),
                                                      10,
                                                      10);
    QVERIFY2(theme, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCOMPARE(thumbBand->height(), theme->property("menuHeight").toInt());
}

// The «Select all» button is the app's button — the icon-and-caption one of
// the header — and it stands centered in the menu band.
void TestPanelMenusQml::theSelectAllButtonIsTheAppsButton()
{
    const std::unique_ptr<QObject> thumb = loadFile(QStringLiteral("MainWindow/ThumbPanel.qml"), 200, 600);
    QVERIFY2(thumb, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QQuickItem *button = selectAllButtonOf(thumb.get());
    QVERIFY2(button, "the thumbnail menu must have the «Select all» button");
    QVERIFY2(findByClassName(button, QLatin1String("Icon")), "the button must carry a glyph");

    // The app's button height: the icon-and-caption button of the header.
    const std::unique_ptr<QObject> appButton = loadFile(QStringLiteral("Common/IconToolButton.qml"), 200, 40);
    QVERIFY2(appButton, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCOMPARE(button->height(), appButton->property("implicitHeight").toInt());

    // The band centers what it holds.
    QQuickItem *band = bandOf(thumb.get());
    QVERIFY(band);
    QCOMPARE(button->y() + button->height() / 2.0, band->height() / 2.0);
}

// The zoom of the page panel stands in the toolbar: the two signs and the
// percentage between them, with no dropdown behind a chevron (ADR 324).
void TestPanelMenusQml::theZoomStandsInTheToolbar()
{
    const std::unique_ptr<QObject> page = loadInline(QStringLiteral(R"(
import QtQuick
import QtQuick.Layouts
import LLocr

ColumnLayout {
    PageToolBar {
        id: bar
        Layout.fillWidth: true
        preview: QtObject {
            property real zoom: 1.25
            function zoomBy(factor) { zoom = zoom * factor }
            function resetZoom() { zoom = 1 }
        }
    }
}
)"),
                                                     400,
                                                     600);
    QVERIFY2(page, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *bar = findByClassName(page.get(), QLatin1String("PageToolBar"));
    QVERIFY2(bar, "the page panel must have its toolbar");

    // The two signs stand in the toolbar.
    QObject *minusButton = findByProperty(bar, "iconName", QStringLiteral("minus"));
    QObject *plusButton = findByProperty(bar, "iconName", QStringLiteral("plus"));
    QVERIFY2(minusButton, "the toolbar must carry the «minus» sign");
    QVERIFY2(plusButton, "the toolbar must carry the «plus» sign");

    // The dropdown is gone: the blocks menu is the only menu of the toolbar.
    QCOMPARE(countMenus(bar), 1);

    // The percentage between the signs says the zoom.
    QVERIFY2(findText(bar, QStringLiteral("125%")), "the toolbar must show the zoom in percent");

    // The signs scale the page by the editor's step.
    QMetaObject::invokeMethod(minusButton, "click");
    QTest::qWait(20);
    QVERIFY(qAbs(zoomOf(bar) - 1.0) < 0.0001);
    QVERIFY2(findText(bar, QStringLiteral("100%")), "the percentage must follow the zoom");

    QMetaObject::invokeMethod(plusButton, "click");
    QTest::qWait(20);
    QVERIFY(qAbs(zoomOf(bar) - 1.25) < 0.0001);
    QVERIFY2(findText(bar, QStringLiteral("125%")), "the percentage must follow the zoom");

    // The percentage returns the page to the fit.
    QObject *percentText = findText(bar, QStringLiteral("125%"));
    QVERIFY(percentText);
    QQuickItem *percentButton = qobject_cast<QQuickItem *>(percentText)->parentItem();
    QVERIFY(percentButton);
    QMetaObject::invokeMethod(percentButton, "click");
    QTest::qWait(20);
    QVERIFY(qAbs(zoomOf(bar) - 1.0) < 0.0001);
}

// The work panel's switch has three positions: the raw markdown, the
// rendered preview and the editor (ADR 331).
void TestPanelMenusQml::theSwitchOffersThePreview()
{
    const std::unique_ptr<QObject> panel = loadFile(QStringLiteral("MainWindow/WorkPanel.qml"), 400, 600);
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *viewSwitch = findByClassName(panel.get(), QLatin1String("SegmentedControl"));
    QVERIFY2(viewSwitch, "the panel must carry its view switch");

    const QVariantList options = viewSwitch->property("options").toList();
    QCOMPARE(options.size(), 3);
    QCOMPARE(options.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Text (MD)"));
    QCOMPARE(options.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Preview"));
    QCOMPARE(options.at(2).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("WYSIWYG"));
    QCOMPARE(viewSwitch->property("currentIndex").toInt(), 0);
}

QTEST_MAIN(TestPanelMenusQml)
#include "test_panel_menus_qml.moc"
