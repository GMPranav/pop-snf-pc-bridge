#ifndef GPU_DETECT_H
#define GPU_DETECT_H

#include <string>
#include <vector>
#include <cstdint>

struct GpuAdapterInfo {
    std::string name;              // Real hardware name queried dynamically via DXGI
    uint64_t dedicated_vram_mb;    // Dedicated video memory in MB
    bool is_integrated;            // True if integrated / shared memory
    bool is_discrete;              // True if dedicated high-performance GPU
    int preference_val;            // 1 for Power Saving (iGPU), 2 for High Performance (dGPU)
};

struct SystemGpuTopology {
    std::vector<GpuAdapterInfo> adapters;
    int integrated_index = -1;     // Index into adapters (-1 if none)
    int discrete_index = -1;       // Index into adapters (-1 if none)
    bool is_hybrid_system = false; // True if both integrated and discrete GPUs are present
    int recommended_preference = 0;// 1 if hybrid (recommends iGPU for tear-free VSync), 0 if single GPU
};

// Dynamically queries all hardware GPUs in the system via DXGI without any hardcoded names.
const SystemGpuTopology& get_system_gpu_topology();

// Checks if the active OpenGL context is running on the system's integrated GPU.
bool is_active_gpu_integrated(const char *gl_vendor, const char *gl_renderer);

#endif // GPU_DETECT_H
