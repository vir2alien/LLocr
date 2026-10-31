#include "runtime/LogDocumentSync.h"

#include <QTextCursor>
#include <QTextDocument>

namespace llocr {

void LogDocumentSync::attach(QTextDocument *doc)
{
    m_doc = doc;
    if (!doc)
        return;
    doc->setUndoRedoEnabled(false);
    m_inDoc = 0;
    m_docLines = 0;
    m_hasHeader = false;
    m_headerText.clear();
    m_pendingFull = true;
}

void LogDocumentSync::apply(const QStringList &lines, int dropped, const QString &header, bool forceFull)
{
    QTextDocument *doc = m_doc.data();
    if (!doc || m_paused)
        return;
    if (forceFull || m_pendingFull) {
        m_pendingFull = false;
        rebuild(*doc, lines, header);
        return;
    }

    if (dropped > 0) {
        QTextCursor cur(doc);
        cur.movePosition(QTextCursor::Start);
        if (m_hasHeader && !cur.movePosition(QTextCursor::NextBlock))
            return;
        for (int moved = 0; moved < dropped; ++moved) {
            if (!cur.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor))
                break;
        }
        cur.removeSelectedText();
        m_inDoc = qMax(0, m_inDoc - dropped);
        m_docLines = qMax(0, m_docLines - dropped);
    }

    const bool wantHeader = !header.isEmpty();
    if (wantHeader != m_hasHeader) {
        QTextCursor cur(doc);
        cur.movePosition(QTextCursor::Start);
        if (wantHeader) {
            if (m_docLines > 0)
                cur.insertBlock();
            cur.insertText(header);
            m_hasHeader = true;
            m_headerText = header;
            ++m_docLines;
        } else {
            QTextCursor cur2(doc);
            cur2.movePosition(QTextCursor::Start);
            if (cur2.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor))
                cur2.removeSelectedText();
            m_hasHeader = false;
            m_headerText.clear();
            m_docLines = qMax(0, m_docLines - 1);
        }
    } else if (wantHeader && m_headerText != header) {
        QTextCursor cur(doc);
        cur.movePosition(QTextCursor::Start);
        cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cur.insertText(header);
        m_headerText = header;
    }

    // A reset (clear) empties the mirror without a dropped counter — the one
    // shrink the append path cannot express, so fall back to a rebuild.
    if (lines.size() < m_inDoc) {
        rebuild(*doc, lines, header);
        return;
    }

    QTextCursor cur(doc);
    cur.movePosition(QTextCursor::End);
    for (int i = m_inDoc; i < lines.size(); ++i) {
        if (m_docLines > 0)
            cur.insertBlock();
        cur.insertText(lines.at(i));
        ++m_docLines;
    }
    m_inDoc = lines.size();
}

void LogDocumentSync::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    // Every flush while paused was skipped: the counters describe a document
    // the current list has moved past, so the first apply after resuming
    // rebuilds from the full state.
    if (!paused)
        m_pendingFull = true;
}

void LogDocumentSync::rebuild(QTextDocument &doc, const QStringList &lines, const QString &header)
{
    // One replacement, one contentsChanged: the attached QQuickTextEdit
    // re-lays-out the whole document per contentsChanged, so per-line cursor
    // inserts cost a full layout each — seconds on a saturated ring, right in
    // the handler that opens the window.
    QString text = lines.join(QLatin1Char('\n'));
    if (!header.isEmpty())
        text = text.isEmpty() ? header : header + QLatin1Char('\n') + text;
    doc.setPlainText(text);
    m_docLines = lines.size();
    m_hasHeader = false;
    m_headerText.clear();
    if (!header.isEmpty()) {
        m_hasHeader = true;
        m_headerText = header;
        ++m_docLines;
    }
    m_inDoc = lines.size();
}

}  // namespace llocr
