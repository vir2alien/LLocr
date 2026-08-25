#pragma once

#include <QString>

namespace llocr {

// Outcome of extracting a ZIP archive (§ Stage D task 4).
struct ExtractResult {
    bool ok = false;
    QString error;         // human-readable failure (or empty on success)
    QString warning;       // non-fatal note (e.g. missing published sha265, skipped link)
    int fileCount = 0;     // regular files written
    qint64 totalBytes = 0; // total uncompressed payload written
};

// Extracts a .zip archive into `destDir` with a strict hardening policy. Only
// ZIP (stored = method 0 and deflate = method 8) is supported; .tar.gz and
// other containers are rejected outright (ADR 34). No syscall `tar`, no
// private Qt headers.
//
// Hardening enforced (§5 task 4):
//   - entry names are normalized and rejected when they contain `..`, drive
//     prefixes, or absolute paths; `\` separators, UNC and control characters
//     are always rejected;
//   - symlink / hardlink entries are rejected entirely (link targets are not
//     followed or dereferenced);
//   - anti-bomb limits: cumulative uncompressed size ≤ 4 GiB, entry count
//     ≤ 10 000, per-entry and running compression ratio ≤ 200:1;
//   - a file may not overwrite another entry already extracted in this run
//     (duplicate normalized paths are rejected);
//   - the CRC-32 recorded per entry is verified; chmod 0755 is applied only to
//     executable entries, and only beneath `destDir`.
class ArchiveExtractor
{
public:
    static constexpr qint64 kMaxTotalBytes = 4LL * 1024 * 1024 * 1024;  // 4 GiB
    static constexpr int kMaxFiles = 10000;
    static constexpr int kMaxCompressionRatio = 200;

    static ExtractResult extractZip(const QString &zipPath, const QString &destDir);
};

}  // namespace llocr