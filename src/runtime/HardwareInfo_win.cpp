#include <dxgi.h>
#include <windows.h>

#include <algorithm>

#include "runtime/HardwareInfo.h"

namespace llocr {

// DXGI is loaded on demand: the app itself never renders with D3D, and a
// missing dxgi.dll (stripped Windows builds) must not cost a startup crash.
QList<GpuAdapter> HardwareInfo::detectAdapters()
{
    QList<GpuAdapter> adapters;
    HMODULE dxgi = ::LoadLibraryW(L"dxgi.dll");
    if (!dxgi)
        return adapters;

    using CreateFactoryFn = HRESULT(WINAPI *)(const IID &, void **);
    const auto createFactory = reinterpret_cast<CreateFactoryFn>(::GetProcAddress(dxgi, "CreateDXGIFactory1"));
    if (!createFactory) {
        ::FreeLibrary(dxgi);
        return adapters;
    }

    IDXGIFactory1 *factory = nullptr;
    if (SUCCEEDED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) {
        for (UINT i = 0;; ++i) {
            IDXGIAdapter1 *adapter = nullptr;
            if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 desc = {};
            adapter->GetDesc1(&desc);
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                GpuAdapter entry;
                entry.name = QString::fromWCharArray(desc.Description);
                entry.dedicatedBytes = static_cast<qint64>(desc.DedicatedVideoMemory);
                entry.vendorId = desc.VendorId;
                adapters.push_back(entry);
            }
            adapter->Release();
        }
        factory->Release();
    }
    ::FreeLibrary(dxgi);

    // Hybrid laptops list the integrated adapter first; the largest dedicated
    // memory is the adapter the model would actually run on.
    std::stable_sort(adapters.begin(), adapters.end(), [](const GpuAdapter &a, const GpuAdapter &b) { return a.dedicatedBytes > b.dedicatedBytes; });
    return adapters;
}

}  // namespace llocr
