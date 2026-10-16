#include "runtime/HardwareInfo.h"

namespace llocr {

QList<GpuAdapter> HardwareInfo::detectAdapters()
{
    // No Linux GPU enumeration: the platform keeps the CPU default and Vulkan
    // stays a manual choice in the backend list.
    return {};
}

}  // namespace llocr
