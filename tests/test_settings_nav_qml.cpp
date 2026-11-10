// The section list on the left of the settings window (ADR 297).
//
// The nav is a plain list: the window owns the sections, the nav reports the
// pick. The test loads the shipped QML from the source tree and clicks the
// real rows — the list is what the user touches.

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include "app/UiController.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

class TestSettingsNavQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void theListShowsTheFiveSections();
    void aClickReportsThePickedSection();

private:
    std::unique_ptr<QObject> loadNav();
    QObject *itemAt(QObject *nav, int index);
    void clickItem(QObject *item);

    QString m_qmlRoot;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_parent = nullptr;
    QStringList m_warnings;
};

void TestSettingsNavQml::initTestCase()
{
    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    m_settings = std::make_unique<SettingsStore>();
    m_uiController = std::make_unique<UiController>(*m_settings);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    const auto registerFile = [](const QString &path, const char *name, bool singleton) {
        const QUrl url = QUrl::fromLocalFile(path);
        if (singleton)
            qmlRegisterSingletonType(url, "LLocr", 1, 0, name);
        else
            qmlRegisterType(url, "LLocr", 1, 0, name);
    };
    registerFile(m_qmlRoot + QStringLiteral("/Theme.qml"), "Theme", true);
    registerFile(m_qmlRoot + QStringLiteral("/Common/Icon.qml"), "Icon", false);
    registerFile(m_qmlRoot + QStringLiteral("/Common/LLOLabel.qml"), "LLOLabel", false);
    registerFile(m_qmlRoot + QStringLiteral("/Settings/SettingsSectionNav.qml"), "SettingsSectionNav", false);

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

void TestSettingsNavQml::theListShowsTheFiveSections()
{
    std::unique_ptr<QObject> nav = loadNav();
    QVERIFY(nav);
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("\n"))));

    QVariant count = nav->property("count");
    QCOMPARE(count.toInt(), 5);

    const QVariantList sections = nav->property("sections").toList();
    QCOMPARE(sections.size(), 5);
    QCOMPARE(sections.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("interface"));
    QCOMPARE(sections.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("output"));
    QCOMPARE(sections.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("verification"));
    QCOMPARE(sections.at(3).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("runtime"));
    QCOMPARE(sections.at(4).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("models"));
    // Every row carries a glyph that exists in the icon set.
    for (const QVariant &entry : sections) {
        const QString icon = entry.toMap().value(QStringLiteral("icon")).toString();
        const QString id = entry.toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(!icon.isEmpty(), qPrintable(id));
        const QString glyph = QFINDTESTDATA(QStringLiteral("../../resources/icons/ui/") + icon + QStringLiteral(".svg"));
        QVERIFY2(!glyph.isEmpty(), qPrintable(icon));
    }
}

void TestSettingsNavQml::aClickReportsThePickedSection()
{
    std::unique_ptr<QObject> nav = loadNav();
    QVERIFY(nav);

    QSignalSpy spy(nav.get(), SIGNAL(sectionPicked(QString)));
    QObject *row = itemAt(nav.get(), 2);
    QVERIFY(row);
    clickItem(row);

    QCOMPARE(nav->property("currentIndex").toInt(), 2);
    QCOMPARE(nav->property("currentSectionId").toString(), QStringLiteral("verification"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("verification"));
}

std::unique_ptr<QObject> TestSettingsNavQml::loadNav()
{
    m_warnings.clear();

    // The window owns the sections; the test plays the window's part.
    QVariantList sections;
    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("interface"), QStringLiteral("Interface")},
        {QStringLiteral("output"), QStringLiteral("Output")},
        {QStringLiteral("verification"), QStringLiteral("Verification")},
        {QStringLiteral("runtime"), QStringLiteral("Runtime")},
        {QStringLiteral("models"), QStringLiteral("Models")},
    };
    for (const auto &row : rows) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), row.first);
        entry.insert(QStringLiteral("title"), row.second);
        entry.insert(QStringLiteral("icon"),
                     row.first == QStringLiteral("interface")      ? QStringLiteral("monitor")
                     : row.first == QStringLiteral("output")       ? QStringLiteral("file-text")
                     : row.first == QStringLiteral("verification") ? QStringLiteral("circle-check")
                     : row.first == QStringLiteral("runtime")      ? QStringLiteral("server")
                                                                   : QStringLiteral("box"));
        sections.append(entry);
    }

    const QString path = QDir(m_qmlRoot).filePath(QStringLiteral("Settings/SettingsSectionNav.qml"));
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    if (!component.isReady()) {
        m_warnings.append(component.errorString());
        return nullptr;
    }

    // The sections are a required property: they come with the object.
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{QStringLiteral("sections"), sections}}));
    if (!object) {
        m_warnings.append(component.errorString());
        return nullptr;
    }
    if (auto *item = qobject_cast<QQuickItem *>(object.get())) {
        item->setParentItem(m_parent);
        item->setWidth(200);
        item->setHeight(400);
    }
    // The delegates are created in the polish: let it happen before the test
    // asks for a row.
    QTest::qWait(20);
    return object;
}

QObject *TestSettingsNavQml::itemAt(QObject *nav, int index)
{
    // A delegate is not a child of the ListView: it comes from itemAtIndex().
    QQmlExpression expression(QQmlEngine::contextForObject(nav), nav, QStringLiteral("itemAtIndex(%1)").arg(index));
    return expression.evaluate().value<QObject *>();
}

void TestSettingsNavQml::clickItem(QObject *item)
{
    auto *quick = qobject_cast<QQuickItem *>(item);
    QVERIFY(quick);
    const QPointF center = quick->mapToScene(QPointF(quick->width() / 2.0, quick->height() / 2.0));
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, center.toPoint());
}

QTEST_MAIN(TestSettingsNavQml)
#include "test_settings_nav_qml.moc"
