#import <Metal/Metal.h>

#include "runtime/HardwareInfo.h"

namespace llocr {

// On unified memory the Metal working-set size is the share of RAM the GPU
// may actually use — the honest VRAM analogue for a quantization budget.
QList<GpuAdapter> HardwareInfo::detectAdapters()
{
    QList<GpuAdapter> adapters;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
        return adapters;
    GpuAdapter adapter;
    adapter.name = QString::fromNSString(device.name);
    adapter.dedicatedBytes = static_cast<qint64>(device.recommendedMaxWorkingSetSize);
    adapters.push_back(adapter);
    return adapters;
}

}  // namespace llocr
