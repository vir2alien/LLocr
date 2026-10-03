#pragma once

class QLockFile;

namespace llocr {

class InstallLockGuard
{
public:
    explicit InstallLockGuard(QLockFile &lock) : m_lock(lock) {}
    ~InstallLockGuard();

    InstallLockGuard(const InstallLockGuard &) = delete;
    InstallLockGuard &operator=(const InstallLockGuard &) = delete;

    bool tryLock();
    bool held() const { return m_held; }
    void release();

private:
    QLockFile &m_lock;
    bool m_held = false;
};

}  // namespace llocr