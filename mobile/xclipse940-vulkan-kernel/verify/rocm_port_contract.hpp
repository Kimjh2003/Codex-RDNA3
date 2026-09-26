#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>

// Version 1 of this project's portable compute contract. These types are
// deliberately independent of HIP, HSA, and Vulkan binary ABIs.
namespace rocm_port {

inline constexpr uint32_t contract_version = 1;

enum class MemoryClass : uint8_t {
    host_visible_coherent,
    device_local,
    sampled_image,
};

struct AgentCaps {
    uint32_t api_major;
    uint32_t api_minor;
    uint32_t api_patch;
    uint32_t max_group_invocations;
    std::array<uint32_t, 3> max_local_size;
    std::array<uint32_t, 3> max_group_count;
    bool compute_queue;
    bool astc_8x8_srgb_sampled;
};

struct KernelSpec {
    const char* name;
    std::array<uint32_t, 3> local_size;
    uint32_t argument_bytes;
};

struct DispatchPacket {
    const KernelSpec* kernel;
    std::array<uint32_t, 3> group_count;
};

// A completion token belongs to the whole submission, not an individual
// packet. The current Vulkan backend realizes it with one VkFence.
struct Submission {
    uint64_t completion_token;
    uint32_t packet_count;
};

inline void validate(const AgentCaps& agent, const DispatchPacket& packet) {
    if (!agent.compute_queue || !packet.kernel || !packet.kernel->name)
        throw std::runtime_error("compute agent/kernel unavailable");
    uint64_t local_product = 1;
    for (uint32_t axis = 0; axis < 3; ++axis) {
        const uint32_t local = packet.kernel->local_size[axis];
        const uint32_t groups = packet.group_count[axis];
        if (!local || local > agent.max_local_size[axis] ||
            !groups || groups > agent.max_group_count[axis])
            throw std::runtime_error("dispatch dimension outside agent limits");
        local_product *= local;
    }
    if (local_product > agent.max_group_invocations)
        throw std::runtime_error("workgroup exceeds agent limit");
}

} // namespace rocm_port
