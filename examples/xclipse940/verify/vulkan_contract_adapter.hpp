#pragma once

#include "rocm_port_contract.hpp"
#include <vulkan/vulkan.h>

#include <cstdio>
#include <stdexcept>

namespace rocm_port {

struct VulkanKernelBinding {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSet descriptors;
};

inline void encode_dispatch(VkCommandBuffer command, const AgentCaps& agent,
                            const DispatchPacket& packet,
                            const VulkanKernelBinding& binding,
                            const void* arguments, uint32_t argument_bytes) {
    validate(agent, packet);
    if (!arguments || argument_bytes != packet.kernel->argument_bytes)
        throw std::runtime_error("kernel argument layout mismatch");
    std::printf("dispatch kernel=%s groups=%u,%u,%u local=%u,%u,%u\n",
                packet.kernel->name,
                packet.group_count[0], packet.group_count[1], packet.group_count[2],
                packet.kernel->local_size[0], packet.kernel->local_size[1],
                packet.kernel->local_size[2]);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, binding.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, binding.layout,
                            0, 1, &binding.descriptors, 0, nullptr);
    vkCmdPushConstants(command, binding.layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, argument_bytes, arguments);
    vkCmdDispatch(command, packet.group_count[0], packet.group_count[1],
                  packet.group_count[2]);
}

inline void submit_and_wait(VkDevice device, VkQueue queue, VkCommandBuffer command,
                            const Submission& submission, uint64_t timeout_ns) {
    if (!submission.completion_token || !submission.packet_count)
        throw std::runtime_error("invalid submission contract");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    VkResult result = vkCreateFence(device, &fence_info, nullptr, &fence);
    if (result != VK_SUCCESS) throw std::runtime_error("create completion fence failed");
    result = vkQueueSubmit(queue, 1, &submit, fence);
    if (result == VK_SUCCESS)
        result = vkWaitForFences(device, 1, &fence, VK_TRUE, timeout_ns);
    vkDestroyFence(device, fence, nullptr);
    if (result != VK_SUCCESS) throw std::runtime_error("submission/fence wait failed");
    std::printf("submission token=%llu packets=%u complete (Vulkan fence)\n",
                static_cast<unsigned long long>(submission.completion_token),
                submission.packet_count);
}

} // namespace rocm_port
