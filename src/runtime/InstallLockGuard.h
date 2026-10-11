#pragma once

#include <functional>
#include <QString>

class QLockFile;

namespace llocr {

// The install lock lives in InstalledState and is recreated whenever the
// runtime directory moves, so the guard resolves the QLockFile through the
// provider on every use — a bound reference would dangle after such a rebind.
class InstallLockGuard
{
public:
    using Provider = std::function<QLockFile &()>;

    explicit InstallLockGuard(Provider provider) : m_provider(std::move(provider)) {}
    ~InstallLockGuard();

    InstallLockGuard(const InstallLockGuard &) = delete;
    InstallLockGuard &operator=(const InstallLockGuard &) = delete;

    bool tryLock();
    bool held() const;
    void release();

private:
    Provider m_provider;
    bool m_held = false;
    QString m_lockedFile;  ///< QLockFile tryLock() locked; empty while idle
};

}  // namespace llocr
