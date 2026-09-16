#pragma once

#include <QString>

namespace llocr {

struct ExtractResult {
    bool ok = false;
    QString error;         // human-readable failure (or empty on success)
    QString warning;       // non-fatal note (e.g. missing published sha256, skipped link)
    int fileCount = 0;     // regular files written
    qint64 totalBytes = 0; // total uncompressed payload written
};

class ArchiveExtractor
{
public:
    static constexpr qint64 kMaxTotalBytes = 4LL * 1024 * 1024 * 1024;  // 4 GiB
    static constexpr int kMaxFiles = 10000;
    static constexpr int kMaxCompressionRatio = 200;

    static ExtractResult extractArchive(const QString &archivePath,
                                        const QString &destDir);

    static ExtractResult extractZip(const QString &zipPath, const QString &destDir);
    static ExtractResult extractTarGz(const QString &tarGzPath, const QString &destDir);
};

}  // namespace llocr