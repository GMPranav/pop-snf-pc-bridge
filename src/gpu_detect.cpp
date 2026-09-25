#include "gpu_detect.h"
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <dxgi.h>

static SystemGpuTopology s_topology;
static bool s_topology_cached = false;

static std::string wide_to_utf8(const wchar_t *wstr) {
    if (!wstr) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0) return "";
    std::string str(size_needed - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], size_needed, NULL, NULL);
    return str;
}

const SystemGpuTopology& get_system_gpu_topology() {
    if (s_topology_cached) {
        return s_topology;
    }
    s_topology_cached = true;
    s_topology = SystemGpuTopology();

    HMODULE hDxgi = LoadLibraryA("dxgi.dll");
    if (!hDxgi) {
        return s_topology;
    }

    typedef HRESULT (WINAPI *PFN_CreateDXGIFactory1)(REFIID, void**);
    PFN_CreateDXGIFactory1 pfnCreate = (PFN_CreateDXGIFactory1)(void*)GetProcAddress(hDxgi, "CreateDXGIFactory1");
    if (!pfnCreate) {
        FreeLibrary(hDxgi);
        return s_topology;
    }

    IDXGIFactory1 *pFactory = NULL;
    if (FAILED(pfnCreate(__uuidof(IDXGIFactory1), (void**)&pFactory)) || !pFactory) {
        FreeLibrary(hDxgi);
        return s_topology;
    }

    IDXGIAdapter1 *pAdapter = NULL;
    for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
            // Skip Microsoft Basic Render Driver and software emulation adapters
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                GpuAdapterInfo info;
                info.name = wide_to_utf8(desc.Description);
                info.dedicated_vram_mb = (uint64_t)(desc.DedicatedVideoMemory / (1024 * 1024));

                // On modern Windows systems, discrete graphics cards have dedicated VRAM (>= 1GB).
                // Integrated GPUs share system memory and report minimal dedicated VRAM (e.g. 128MB or 512MB).
                if (info.dedicated_vram_mb >= 1024) {
                    info.is_discrete = true;
                    info.is_integrated = false;
                    info.preference_val = 2; // High Performance
                } else {
                    info.is_discrete = false;
                    info.is_integrated = true;
                    info.preference_val = 1; // Power Saving
                }

                s_topology.adapters.push_back(info);
            }
        }
        pAdapter->Release();
    }
    pFactory->Release();
    FreeLibrary(hDxgi);

    for (size_t i = 0; i < s_topology.adapters.size(); ++i) {
        if (s_topology.adapters[i].is_integrated && s_topology.integrated_index == -1) {
            s_topology.integrated_index = (int)i;
        }
        if (s_topology.adapters[i].is_discrete && s_topology.discrete_index == -1) {
            s_topology.discrete_index = (int)i;
        }
    }

    // Hybrid laptops have both an integrated GPU (display-attached) and a discrete GPU (PCIe-attached).
    if (s_topology.integrated_index != -1 && s_topology.discrete_index != -1) {
        s_topology.is_hybrid_system = true;
        // On hybrid laptops, the integrated GPU is directly wired to the screen and guarantees tear-free VSync.
        s_topology.recommended_preference = 1;
    } else {
        s_topology.is_hybrid_system = false;
        // On single-GPU systems (e.g. desktop PCs), let the OS use the default adapter without overriding.
        s_topology.recommended_preference = 0;
    }

    return s_topology;
}

bool is_active_gpu_integrated(const char *gl_vendor, const char *gl_renderer) {
    if (!gl_renderer && !gl_vendor) return false;
    const SystemGpuTopology &topo = get_system_gpu_topology();

    if (topo.is_hybrid_system && topo.integrated_index != -1) {
        const std::string &igpu_name = topo.adapters[topo.integrated_index].name;
        if (gl_renderer && strstr(gl_renderer, igpu_name.c_str()) != NULL) {
            return true;
        }
    }

    // Generic heuristic for integrated graphics (Intel HD/UHD/Iris, AMD APU Radeon(TM) Graphics)
    if (gl_vendor && strstr(gl_vendor, "Intel")) return true;
    if (gl_renderer && (strstr(gl_renderer, "Intel") || strstr(gl_renderer, "Iris") || strstr(gl_renderer, "HD Graphics") || strstr(gl_renderer, "UHD Graphics"))) return true;
    if (gl_renderer && strstr(gl_renderer, "Radeon(TM) Graphics")) return true;

    return false;
}
#else
// Non-Windows stub
const SystemGpuTopology& get_system_gpu_topology() {
    static SystemGpuTopology empty_topo;
    return empty_topo;
}

bool is_active_gpu_integrated(const char *gl_vendor, const char *gl_renderer) {
    if (gl_vendor && strstr(gl_vendor, "Intel")) return true;
    if (gl_renderer && (strstr(gl_renderer, "Intel") || strstr(gl_renderer, "Iris"))) return true;
    return false;
}
#endif
