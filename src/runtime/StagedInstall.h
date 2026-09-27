#pragma once

#include <QString>

namespace llocr {

/// A directory that is only ever published by an atomic rename.
///
/// Everything is written into `stagingPath()`; `commit()` moves an existing
/// directory aside, renames the staging directory into place and drops the
/// backup (restoring it if the rename fails). Destroying the object without a
/// commit removes the staging directory, so a failed or cancelled install can
/// never leave a half-populated directory behind for the next scan to adopt as a
/// valid install (ADR 112).
///
/// The runtime install used this inline with a uuid staging directory; the model
/// install had no staging at all and wrote straight into its final directory.
class StagedInstall
{
public:
    /// `stagingPath` is created; `finalDir` is the publish target. The parent
    /// directory of `finalDir` must exist.
    StagedInstall(const QString &stagingPath, const QString &finalDir);
    ~StagedInstall();

    StagedInstall(const StagedInstall &) = delete;
    StagedInstall &operator=(const StagedInstall &) = delete;

    bool isValid() const { return m_error.isEmpty(); }
    const QString &error() const { return m_error; }
    const QString &stagingPath() const { return m_stagingPath; }
    const QString &finalPath() const { return m_finalPath; }

    /// For callers that only learn the publish target while working (the runtime
    /// install names the directory after probing the extracted binary). Must be
    /// called before commit().
    void setFinalPath(const QString &finalDir) { m_finalPath = finalDir; }

    /// Publishes the staging directory. Safe to call once; a second call is
    /// refused rather than moving something again.
    bool commit(QString *error = nullptr);

    /// Removes the staging directory now (idempotent).
    void discard();

    /// Staging directory for a named artifact below the runtime staging root.
    /// Deterministic on purpose: a resumed install finds its own `.part` files,
    /// and the install lock keeps a second install of the same artifact out.
    static QString stagingPathFor(const QString &stagingRoot, const QString &name);

private:
    QString m_stagingPath;
    QString m_finalPath;
    QString m_error;
    bool m_committed = false;
};

}  // namespace llocr
