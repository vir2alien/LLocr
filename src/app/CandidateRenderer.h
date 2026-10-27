#pragma once

#include <functional>
#include <memory>
#include <QImage>
#include <QObject>
#include <QString>

class QTimer;
class QUrl;
class QWebEnginePage;
class QTemporaryDir;

namespace llocr {

// Renders one block's candidate text back into an image, the second leg of the
// visual-diff decision (docs/31): the decision model compares the crop with
// the picture the candidate paints, so the render has to show what the
// candidate means — text as paragraphs, formulas as typeset math, tables as
// ruled grids. The markdown stack of the preview page (marked + KaTeX) draws
// it; the page is printed to a one-page PDF and rasterized.
class CandidateRenderer : public QObject
{
    Q_OBJECT

public:
    using ResultCallback = std::function<void(bool ok, const QImage &image, const QString &error)>;

    explicit CandidateRenderer(QObject *parent = nullptr);
    ~CandidateRenderer() override;

    bool isBusy() const { return m_busy; }
    void render(const QString &text, const ResultCallback &callback);

private:
    void ensurePage();
    void startRender();
    void startFontWait();
    void pollFonts();
    void measure();
    void startPrint();
    void rasterize();
    void runJs(const QString &script, const std::function<void(const QVariant &)> &onResult);
    void fail(const QString &error);
    void finish(bool success, const QImage &image, const QString &error);
    static QString jsonString(const QString &value);

private:
    std::unique_ptr<QWebEnginePage> m_page;
    bool m_pageReady = false;

    std::unique_ptr<QTemporaryDir> m_temp;
    bool m_busy = false;
    QString m_text;
    QString m_pdfPath;
    int m_widthPx = 0;
    int m_heightPx = 0;
    ResultCallback m_callback;

    QTimer *m_pollTimer = nullptr;
    QTimer *m_watchdog = nullptr;
    int m_pollCount = 0;
};

}  // namespace llocr
