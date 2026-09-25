#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QString>

struct ddjvu_context_s;
struct ddjvu_document_s;
struct ddjvu_job_s;

namespace llocr {

class DjVuDocument final
{
    Q_DECLARE_TR_FUNCTIONS(DjVuDocument)

public:
    DjVuDocument() = default;
    ~DjVuDocument();
    Q_DISABLE_COPY_MOVE(DjVuDocument)

    bool open(const QString& path, QString* error = nullptr);
    int pageCount() const;
    QSize pageSize(int index, QString* error = nullptr);
    QImage render(int index, const QSize& size, QString* error = nullptr);

private:
    void close();
    void drainMessages();
    bool waitForJob(ddjvu_job_s* job);
    void reportError(QString* error, int index = -1) const;

    ddjvu_context_s* m_context = nullptr;
    ddjvu_document_s* m_document = nullptr;
    QString m_path;
    QString m_error;
};

} // namespace llocr
