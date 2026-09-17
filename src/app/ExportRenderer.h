#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <functional>
#include <memory>

#include <QList>
#include <QPageLayout>
#include <QPair>

class QTimer;
class QUrl;
class QWebEnginePage;

namespace llocr {

class ExportRenderer : public QObject
{
    Q_OBJECT

public:
    enum class Output { Html, Pdf };

    using PageInput = QPair<int, QString>;
    using ResultCallback =
        std::function<void(bool success, const QString &html, const QString &error)>;

    struct Request {
        Output output = Output::Html;
        QList<PageInput> pages;
        QString styleSheet;
        QString outputPath;
        bool splitPages = true;
        QPageLayout pageLayout;
    };

    explicit ExportRenderer(QObject *parent = nullptr);
    ~ExportRenderer() override;

    bool isBusy() const { return m_busy; }
    void render(const Request &request, const ResultCallback &callback);

signals:
    void progress(int pagesDone, int pagesTotal);

private:
    class ExportPage;

    void ensurePage();
    void startRun();
    void appendNextPage();
    void startFontWait();
    void pollFonts();
    void deliver();
    void startPdfPrint();
    void runJs(const QString &script,
               const std::function<void(const QVariant &)> &onResult);
    void fail(const QString &error);
    void finish(bool success, const QString &html, const QString &error);
    static QString jsonString(const QString &value);

private:
    std::unique_ptr<ExportPage> m_page;
    bool m_pageReady = false;

    bool m_busy = false;
    Output m_output = Output::Html;
    QList<PageInput> m_pages;
    int m_nextPage = 0;
    QString m_styleSheet;
    bool m_splitPages = true;
    QPageLayout m_pageLayout;
    QString m_outputPath;
    ResultCallback m_callback;

    QTimer *m_pollTimer = nullptr;
    QTimer *m_watchdog = nullptr;
    int m_pollCount = 0;
    bool m_printing = false;
};

} // namespace llocr
