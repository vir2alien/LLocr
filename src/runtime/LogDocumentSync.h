#pragma once

#include <QPointer>
#include <QString>
#include <QStringList>

class QTextDocument;

namespace llocr {

// Mirrors a capped line list into a TextArea document incrementally: appends
// only the new tail, front-trims dropped lines, keeps an optional header line
// (replaced in place when its text changes). Detached updates are no-ops; the
// next attach() does a full rebuild. A full QTextDocument rebuild is reserved
// for attach/clear/retranslate — steady-state flushes are O(new lines).
class LogDocumentSync
{
public:
    void attach(QTextDocument *doc);
    // `lines` is the owner's current full list; `dropped` counts lines fallen
    // off its front since the previous apply; `header` ("" = none) is kept as
    // the first document line and replaced in place when it changes.
    void apply(const QStringList &lines, int dropped, const QString &header, bool forceFull = false);

private:
    void rebuild(QTextDocument &doc, const QStringList &lines, const QString &header);

    QPointer<QTextDocument> m_doc;
    int m_inDoc = 0;     // trailing entries of `lines` already in the document
    int m_docLines = 0;  // content lines in the document, header included
    bool m_hasHeader = false;
    QString m_headerText;
    bool m_pendingFull = false;
};

}  // namespace llocr
