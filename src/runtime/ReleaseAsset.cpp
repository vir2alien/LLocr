
#include "runtime/ReleaseAsset.h"

namespace llocr {

ReleaseAsset ReleaseInfo::pickAsset(QString os, QString arch, QString backend, bool wantCudart) const
{
    for (const ReleaseAsset &a : assets) {
        if (a.cudart != wantCudart)
            continue;
        if (wantCudart)
            return a;
        if (a.os == os && a.arch == arch && (a.backend.isEmpty() || a.backend == backend))
            return a;
    }
    return ReleaseAsset();
}

}  // namespace llocr