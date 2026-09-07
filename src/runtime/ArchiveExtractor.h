#pragma once

#include <QString>

namespace llocr {

// Outcome of extracting an archive (§ Stage D task 4).
struct ExtractResult {
    bool ok = false;
    QString error;         // human-readable failure (or empty on success)
    QString warning;       // non-fatal note (e.g. missing published sha256, skipped link)
    int fileCount = 0;     // regular files written
    qint64 totalBytes = 0; // total uncompressed payload written
};

// Extracts a .zip or .tar.gz archive into `destDir` with a strict hardening
// policy. No syscall `tar`/`unzip`, no private Qt headers.
//
// ZIP (stored = method 0 and deflate = method 8) and tar.gz (gzip + ustar)
// are supported. llama.cpp publishes `.zip` for Windows and `.tar.gz` for
// macOS and Linux (ADR 34 amended — see docs/07-glossary.md).
//
// Hardening enforced (§5 task 4):
//   - entry names are normalized and rejected when they contain `..`, drive
//     prefixes, absolute paths or control characters; `\` separators, UNC
//     and NUL-suffixed names are always rejected;
//   - symlink entries are created only when the link target is a relative
//     plain file name (no `..`, no absolute path) and only beneath `destDir`;
//     hardlinks are skipped with a warning (the llama.cpp macOS tarballs use
//     symlinks for their .dylib version links);
//   - anti-bomb limits: cumulative uncompressed size ≤ 4 GiB, entry count
//     ≤ 10 000, per-entry and running compression ratio ≤ 200:1 (ZIP only);
//   - a file may not overwrite another entry already extracted in this run
//     (duplicate normalized paths are rejected);
//   - ZIP entry CRCs are verified; the gzip trailer is verified by zlib;
//     chmod 0755 is applied only to executable entries, and only beneath
//     `destDir`.
class ArchiveExtractor
{
public:
    static constexpr qint64 kMaxTotalBytes = 4LL * 1024 * 1024 * 1024;  // 4 GiB
    static constexpr int kMaxFiles = 10000;
    static constexpr int kMaxCompressionRatio = 200;

    /// Dispatches to extractZip()/extractTarGz() based on the file extension.
    static ExtractResult extractArchive(const QString &archivePath,
                                        const QString &destDir);

    static ExtractResult extractZip(const QString &zipPath, const QString &destDir);
    static ExtractResult extractTarGz(const QString &tarGzPath, const QString &destDir);
};

}  // namespace llocr