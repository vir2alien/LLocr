// The block panel's header: the divergence notice and the action that settles
// it stand in the header row, next to «Approve block» — the panel used to
// carry them in a row of their own under the version cards (ADR 307).
//
// The panel reads the block's state from the controller singleton; the test
// registers a double (tests/fake_block_controller.qml) with a recognized block
// and a document that diverges from it.

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>

class TestBlockPanelQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void theNoticeAndTheActionStandInTheHeaderRow();
    void theActionRestoresTheBlockText();
    void theNoticeIsGoneWhileTheDocumentFollowsTheBlock();

private:
    std::unique_ptr<QObject> loadPanel();
    QList<QObject *> findAllByClassName(QObject *root, const QLatin1String &className);
    QObject *childWithText(QObject *root, const QLatin1String &className, const QString &text);
    bool isVisible(QObject *item);

    QString m_qmlRoot;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_parent = nullptr;
    QObject *m_controller = nullptr;
    QStringList m_warnings;
};

void TestBlockPanelQml::initTestCase()
{
    // The app runs the Fusion style, and the native macOS style warns about
    // every customized control.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    const QString fake = QFINDTESTDATA(QStringLiteral("fake_block_controller.qml"));
    QVERIFY(!fake.isEmpty());

    // The theme, the card of a version and the controller double are loaded
    // from the source tree, under the module the app uses.
    const auto registerFile = [](const QString &path, const char *name, bool singleton) {
        const QUrl url = QUrl::fromLocalFile(path);
        if (singleton)
            qmlRegisterSingletonType(url, "LLocr", 1, 0, name);
        else
            qmlRegisterType(url, "LLocr", 1, 0, name);
    };
    registerFile(m_qmlRoot + QStringLiteral("/Theme.qml"), "Theme", true);
    registerFile(fake, "Controller", true);
    registerFile(m_qmlRoot + QStringLiteral("/MainWindow/BlockVersionCard.qml"), "BlockVersionCard", false);

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    m_window = std::make_unique<QQuickWindow>();
    m_parent = new QQuickItem(m_window->contentItem());
    m_parent->setSize(QSizeF(700, 320));
    m_window->resize(700, 320);
    m_window->show();

    m_controller = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Controller"));
    QVERIFY(m_controller);
}

std::unique_ptr<QObject> TestBlockPanelQml::loadPanel()
{
    m_warnings.clear();

    const QString path = QDir(m_qmlRoot).filePath(QStringLiteral("MainWindow/BlockEditPanel.qml"));
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
        item->setWidth(700);
        item->setHeight(320);
    }
    // The layout is applied in the polish: let it happen before the test
    // measures the row.
    QTest::qWait(20);
    return object;
}

QList<QObject *> TestBlockPanelQml::findAllByClassName(QObject *root, const QLatin1String &className)
{
    QList<QObject *> found;
    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        // A QML type's class name carries the engine's suffix
        // ("LLOButton_QMLTYPE_23"), so the name is matched by its prefix.
        const QString name = QString::fromLatin1(child->metaObject()->className());
        if (name == className || name.startsWith(className + QStringLiteral("_QMLTYPE")))
            found.append(child);
        found.append(findAllByClassName(child, className));
    }
    return found;
}

QObject *TestBlockPanelQml::childWithText(QObject *root, const QLatin1String &className, const QString &text)
{
    const QList<QObject *> candidates = findAllByClassName(root, className);
    for (QObject *candidate : candidates) {
        if (candidate->property("text").toString() == text)
            return candidate;
    }
    return nullptr;
}

// The item's own flag: an element the panel hides takes no place in the row.
bool TestBlockPanelQml::isVisible(QObject *item)
{
    return item && item->property("visible").toBool();
}

// The notice and the action stand in the header row — the row of the actions,
// the one that holds the close button — and the action stands next to
// «Approve block».
void TestBlockPanelQml::theNoticeAndTheActionStandInTheHeaderRow()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *notice = childWithText(panel.get(), QLatin1String("LLOLabel"), QStringLiteral("The document text differs from the block"));
    QVERIFY2(notice, "the panel must tell that the document differs from the block");

    QObject *restore = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Restore the block text"));
    QVERIFY2(restore, "the panel must offer to restore the block's text");

    QObject *approve = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Approve block"));
    QVERIFY2(approve, "the panel must keep the approve action");

    QObject *close = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("✕"));
    QVERIFY2(close, "the panel must keep the close button");

    // The header row is the row of the actions: the one the close button
    // stands in.
    auto *closeItem = qobject_cast<QQuickItem *>(close);
    auto *noticeItem = qobject_cast<QQuickItem *>(notice);
    auto *restoreItem = qobject_cast<QQuickItem *>(restore);
    auto *approveItem = qobject_cast<QQuickItem *>(approve);
    QVERIFY(closeItem);
    QVERIFY(noticeItem);
    QQuickItem *headerRow = closeItem->parentItem();
    QVERIFY(headerRow);
    QCOMPARE(noticeItem->parentItem(), headerRow);
    QCOMPARE(restoreItem->parentItem(), headerRow);

    // The action stands next to «Approve block», on its left.
    QVERIFY(restoreItem);
    QVERIFY(approveItem);
    QVERIFY(isVisible(restore));
    QVERIFY(isVisible(approve));
    QVERIFY2(restoreItem->x() < approveItem->x(), "the restore action must stand next to the approve one");

    // The actions stay inside the row: the notice must not push them out, and
    // it has the room to say its whole line.
    QVERIFY2(approveItem->x() + approveItem->width() <= headerRow->width() + 1, "the actions must stay inside the header row");
    QVERIFY2(noticeItem->x() >= 0, "the notice must stay inside the header row");
    QVERIFY2(noticeItem->width() >= noticeItem->implicitWidth() - 1, "the notice must say its whole line in a panel of this width");
}

// The action does what it says: it hands the block's text back to the
// document.
void TestBlockPanelQml::theActionRestoresTheBlockText()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *restore = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Restore the block text"));
    QVERIFY(restore);
    auto *item = qobject_cast<QQuickItem *>(restore);
    QVERIFY(item);
    QCOMPARE(m_controller->property("restoreCalls").toInt(), 0);

    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint());
    QCOMPARE(m_controller->property("restoreCalls").toInt(), 1);
}

// While the document follows the block, there is nothing to tell and nothing
// to restore: the notice and the action take no place in the row.
void TestBlockPanelQml::theNoticeIsGoneWhileTheDocumentFollowsTheBlock()
{
    m_controller->setProperty("selectedBlockDocumentDiverged", false);
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QTest::qWait(20);

    QObject *notice = childWithText(panel.get(), QLatin1String("LLOLabel"), QStringLiteral("The document text differs from the block"));
    QVERIFY(notice);
    QVERIFY2(!isVisible(notice), "the notice must be gone while the document follows the block");

    QObject *restore = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Restore the block text"));
    QVERIFY(restore);
    QVERIFY2(!isVisible(restore), "the action must be gone while the document follows the block");

    m_controller->setProperty("selectedBlockDocumentDiverged", true);
}

QTEST_MAIN(TestBlockPanelQml)
#include "test_block_panel_qml.moc"
