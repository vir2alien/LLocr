// The icon set and the component that paints it.
//
// The assets ship with a white stroke and `Common/Icon.qml` colorizes them
// with MultiEffect, which mixes the source by its luminance: a black glyph
// (luminance 0) keeps its color in every theme, so the dark theme showed
// black icons on a black background (ADR 298). The test reads the shipped
// SVGs and the shipped component, so a dark asset or a broken colorization
// fails here instead of in the window.

#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStringList>
#include <QtTest>

#include "app/UiController.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

class TestUiIcons : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void everyShippedGlyphHasALightStroke();
    void theComponentHandsTheRequestedColorToTheEffect();
    void theEffectColorizesTheShippedGlyph();

private:
    QObject *findByClassName(QObject *root, const QLatin1String &className);

    QString m_qmlRoot;
    QString m_iconDir;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QStringList m_warnings;
};

void TestUiIcons::initTestCase()
{
    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());
    m_iconDir = QFINDTESTDATA(QStringLiteral("../resources/icons/ui"));
    QVERIFY(!m_iconDir.isEmpty());

    m_settings = std::make_unique<SettingsStore>();
    m_uiController = std::make_unique<UiController>(*m_settings);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    // The component and the theme it paints itself with are loaded from the
    // source tree, under the module the app uses.
    const auto registerFile = [](const QString &path, const char *name, bool singleton) {
        const QUrl url = QUrl::fromLocalFile(path);
        if (singleton)
            qmlRegisterSingletonType(url, "LLocr", 1, 0, name);
        else
            qmlRegisterType(url, "LLocr", 1, 0, name);
    };
    registerFile(m_qmlRoot + QStringLiteral("/Theme.qml"), "Theme", true);
    registerFile(m_qmlRoot + QStringLiteral("/Common/Icon.qml"), "Icon", false);

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    m_window = std::make_unique<QQuickWindow>();
    m_window->resize(200, 200);
    m_window->show();
}

// The colorization mixes the source by its luminance, so the glyph must be
// light: a dark stroke would keep its own color in both themes.
void TestUiIcons::everyShippedGlyphHasALightStroke()
{
    const QDir dir(m_iconDir);
    const QStringList glyphs = dir.entryList({QStringLiteral("*.svg")}, QDir::Files);
    QVERIFY2(!glyphs.isEmpty(), "the icon set must not be empty");

    const QRegularExpression stroke(QStringLiteral("stroke=\"([^\"]*)\""));

    for (const QString &name : glyphs) {
        QFile file(dir.filePath(name));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(name));
        const QString svg = QString::fromUtf8(file.readAll());

        const QRegularExpressionMatch match = stroke.match(svg);
        QVERIFY2(match.hasMatch(), qPrintable(name));

        const QString value = match.captured(1);
        QVERIFY2(value.startsWith(QLatin1Char('#')), qPrintable(QStringLiteral("%1: the stroke must be a plain color, not %2").arg(name, value)));

        const QColor color(value);
        QVERIFY2(color.isValid(), qPrintable(name));
        const double luminance = 0.299 * color.redF() + 0.587 * color.greenF() + 0.114 * color.blueF();
        QVERIFY2(luminance >= 0.5, qPrintable(QStringLiteral("%1: the stroke must be light, not %2").arg(name, value)));
    }
}

QObject *TestUiIcons::findByClassName(QObject *root, const QLatin1String &className)
{
    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        // A QML type's class name carries the engine's suffix
        // ("QQuickImage_QMLTYPE_23"), so the name is matched by its prefix.
        const QString name = QString::fromLatin1(child->metaObject()->className());
        if (name == className || name.startsWith(className + QStringLiteral("_QMLTYPE")))
            return child;
        if (QObject *found = findByClassName(child, className))
            return found;
    }
    return nullptr;
}

// The icon takes the color its owner asks for — the theme color in the dark
// theme is what the user could not see.
void TestUiIcons::theComponentHandsTheRequestedColorToTheEffect()
{
    m_warnings.clear();

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(m_qmlRoot + QStringLiteral("/Common/Icon.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    std::unique_ptr<QObject> icon(component.create());
    QVERIFY2(icon, qPrintable(m_warnings.join(QStringLiteral("; "))));
    icon->setProperty("name", QStringLiteral("monitor"));
    icon->setProperty("color", QColor(QStringLiteral("#ff8800")));
    QCoreApplication::processEvents();

    QObject *effect = findByClassName(icon.get(), QLatin1String("QQuickMultiEffect"));
    QVERIFY2(effect, "the icon must colorize the glyph with an effect");

    QCOMPARE(effect->property("colorization").toFloat(), 1.0f);
    QCOMPARE(effect->property("colorizationColor").value<QColor>(), QColor(QStringLiteral("#ff8800")));
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("\n"))));
}

// The effect paints the shipped glyph: the source is the icon's own image,
// and that image reads the asset from the icon set.
void TestUiIcons::theEffectColorizesTheShippedGlyph()
{
    m_warnings.clear();

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(m_qmlRoot + QStringLiteral("/Common/Icon.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    std::unique_ptr<QObject> icon(component.create());
    QVERIFY2(icon, qPrintable(m_warnings.join(QStringLiteral("; "))));
    icon->setProperty("name", QStringLiteral("monitor"));
    QCoreApplication::processEvents();

    QObject *image = findByClassName(icon.get(), QLatin1String("QQuickImage"));
    QVERIFY2(image, "the icon must draw the glyph as an image");
    QCOMPARE(image->property("source").toUrl().toString(), QStringLiteral("qrc:/icons/ui/monitor.svg"));

    QObject *effect = findByClassName(icon.get(), QLatin1String("QQuickMultiEffect"));
    QVERIFY2(effect, "the icon must colorize the glyph with an effect");
    QCOMPARE(effect->property("source").value<QObject *>(), image);
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("\n"))));
}

QTEST_MAIN(TestUiIcons)
#include "test_ui_icons.moc"
