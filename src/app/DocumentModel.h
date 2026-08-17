#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include "core/OcrResult.h"

namespace llocr {

/**
 * @brief Holds the opened document
 */

struct DocumentPage {
    QImage image;
    OcrResult result;
    bool recognized = false;
};

class DocumentModel
{
public:
    bool loadImage(const QString& path);
    bool loadImages(const QStringList& paths);
    bool loadPdf(const QString& path);
    bool removePage(int index);
    bool movePage(int from, int to);
    void clear();

    int pageCount() const { return m_pages.size(); }
    bool isEmpty() const { return m_pages.isEmpty(); }

    DocumentPage& page(int index) { return m_pages[index]; }
    const DocumentPage& page(int index) const { return m_pages[index]; }

    bool isValidIndex(int index) const;

private:
    QList<DocumentPage> m_pages;
};

} // namespace llocr
