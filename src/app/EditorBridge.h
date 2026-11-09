#pragma once

#include <QObject>
#include <QString>

namespace llocr {

// The QWebChannel object the editor page calls into. Every notification the
// page sends arrives here and re-emerges as a signal the QML editor wrapper
// subscribes to; nothing else crosses the web boundary.
class EditorBridge : public QObject
{
    Q_OBJECT

public:
    explicit EditorBridge(QObject *parent = nullptr);

signals:
    void pageReady();
    // pushToken identifies the push the text derives from: a pull whose token
    // is no longer the page's current one is an echo of a replaced document.
    void editorTextChanged(const QString &markdown, qint64 pushToken);
    // The per-block pull (ADR 296): the ordinal of the editor's tagged block
    // and its new markdown. The ordinal travels through the block map of the
    // push it belongs to.
    void editorBlockChanged(int blockOrdinal, const QString &markdown, qint64 pushToken);
    // The page skipped a model push while the user was typing and asks for
    // the current state now that its typing has settled.
    void editorResyncRequested();
    void editorReferenceChanged(const QString &markdown, qint64 pushToken);
    void editorFocusChanged(bool focused);
    void editorBlockClicked(int blockIndex);

public slots:
    void notifyPageReady();
    void notifyTextChanged(const QString &markdown, qint64 pushToken);
    void notifyBlockChanged(int blockOrdinal, const QString &markdown, qint64 pushToken);
    void notifyResyncRequested();
    void notifyReferenceChanged(const QString &markdown, qint64 pushToken);
    void notifyEditorFocus(bool focused);
    void notifyBlockClicked(int blockIndex);
};

}  // namespace llocr
