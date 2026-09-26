#ifndef XCLIPSE_GPU_KERNEL_ABI_H
#define XCLIPSE_GPU_KERNEL_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XC_GPU_ABI_VERSION 1u
#define XC_U64_LANES_PER_TILE 32u
#define XC_PIXELS_PER_TILE 64u
#define XC_LOCAL_SIZE_X 32u

typedef struct XcAstcU64Plan {
    uint32_t width;
    uint32_t height;
    uint32_t workgroups_x;
    uint64_t audit_rgba_bytes;
    uint64_t word_plane_bytes;
} XcAstcU64Plan;

// Returns 1 on success. Caller must also check Vulkan image-format support,
// storage-buffer limits, and available memory on the selected device.
static inline int xc_plan_astc_u64(uint32_t width, uint32_t height,
                                   uint32_t max_workgroups_x,
                                   XcAstcU64Plan* out) {
    if (!out || !width || !height) return 0;
    const uint64_t pixels = (uint64_t)width * height;
    if (pixels > UINT32_MAX || pixels % XC_PIXELS_PER_TILE) return 0;
    const uint64_t groups = pixels / XC_PIXELS_PER_TILE;
    if (!groups || groups > max_workgroups_x) return 0;
    out->width = width;
    out->height = height;
    out->workgroups_x = (uint32_t)groups;
    out->audit_rgba_bytes = pixels * sizeof(uint32_t);
    out->word_plane_bytes = pixels * sizeof(uint32_t);
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif
