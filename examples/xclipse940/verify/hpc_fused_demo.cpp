#include <vulkan/vulkan.h>

#include "sve_reference.hpp"
#include "vulkan_contract_adapter.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#if VK_HEADER_VERSION < 304
#error "Vulkan header revision 304 or newer is required"
#endif

static void check(VkResult result, const char* action) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(action) + ": VkResult=" +
                                 std::to_string(result));
}

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error(std::string("cannot open ") + path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static uint32_t le24(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
}

static uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

static uint32_t memory_type(VkPhysicalDevice physical, uint32_t bits,
                            VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    throw std::runtime_error("required Vulkan memory type unavailable");
}

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

static Buffer make_buffer(VkPhysicalDevice physical, VkDevice device,
                          VkDeviceSize bytes, VkBufferUsageFlags usage) {
    Buffer out;
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = bytes;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &create, nullptr, &out.buffer), "create buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, out.buffer, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type(
        physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device, &allocate, nullptr, &out.memory), "allocate buffer");
    check(vkBindBufferMemory(device, out.buffer, out.memory, 0), "bind buffer");
    return out;
}

static void save_file(const char* path, const void* data, size_t bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(data), bytes);
    if (!out) throw std::runtime_error(std::string("cannot write ") + path);
}

int main(int argc, char** argv) {
    try {
        if (argc != 6) {
            std::fprintf(stderr,
                         "usage: %s source.astc fused.spv golden.rgba out.rgba out.words\n",
                         argv[0]);
            return 2;
        }
        const auto astc = read_file(argv[1]);
        const auto shader = read_file(argv[2]);
        const auto golden = read_file(argv[3]);
        if (astc.size() < 16 ||
            std::memcmp(astc.data(), "\x13\xAB\xA1\x5C", 4) ||
            astc[4] != 8 || astc[5] != 8 || astc[6] != 1)
            throw std::runtime_error("expected 2D ASTC 8x8 input");
        const uint32_t width = le24(astc.data() + 7);
        const uint32_t height = le24(astc.data() + 10);
        const size_t pixel_count = size_t(width) * height;
        const size_t compressed_bytes = size_t((width + 7) / 8) *
                                        ((height + 7) / 8) * 16;
        if (!width || !height || width > 4096 || height > 4096 ||
            pixel_count % 64 || astc.size() != compressed_bytes + 16 ||
            shader.empty() || shader.size() % 4 ||
            golden.size() != pixel_count * 4)
            throw std::runtime_error("invalid ASTC, SPIR-V or golden input");
        const uint32_t tiles = uint32_t(pixel_count / 64);
        const VkDeviceSize bytes = pixel_count * 4;

        const SveCapability sve = query_sve_capability();
        if (!sve.sve || !sve.lanes_u64)
            throw std::runtime_error("SVE CPU reference unavailable");
        std::printf("CPU SVE=%u SVE2=%u currentVL=%u maxVL=%u bits "
                    "u64Lanes=%u logicalLanes=32 passesPerTile=%u\n",
                    sve.sve, sve.sve2, sve.current_vl_bits,
                    sve.maximum_vl_bits, sve.lanes_u64,
                    (32 + sve.lanes_u64 - 1) / sve.lanes_u64);

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "ASTC SVE HPC fused demo";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &app;
        VkInstance instance = VK_NULL_HANDLE;
        check(vkCreateInstance(&instance_info, nullptr, &instance), "create instance");
        uint32_t device_count = 0;
        check(vkEnumeratePhysicalDevices(instance, &device_count, nullptr), "enumerate GPU");
        if (!device_count) throw std::runtime_error("no Vulkan GPU");
        std::vector<VkPhysicalDevice> devices(device_count);
        check(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()),
              "enumerate GPU list");
        VkPhysicalDevice physical = devices[0];
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        vkGetPhysicalDeviceFeatures(physical, &features);
        if (VK_API_VERSION_MAJOR(properties.apiVersion) != 1 ||
            VK_API_VERSION_MINOR(properties.apiVersion) < 3)
            throw std::runtime_error("Vulkan 1.3 GPU required");
        constexpr VkFormat format = VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(physical, format, &format_properties);
        if (!features.textureCompressionASTC_LDR ||
            !(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            throw std::runtime_error("ASTC 8x8 sRGB sampling unavailable");
        VkImageFormatProperties image_properties{};
        check(vkGetPhysicalDeviceImageFormatProperties(
                  physical, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  0, &image_properties), "ASTC image support");
        std::printf("GPU=%s Vulkan=%u.%u.%u ASTC_LDR=%u header=%u\n",
                    properties.deviceName,
                    VK_API_VERSION_MAJOR(properties.apiVersion),
                    VK_API_VERSION_MINOR(properties.apiVersion),
                    VK_API_VERSION_PATCH(properties.apiVersion),
                    features.textureCompressionASTC_LDR, VK_HEADER_VERSION);

        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());
        uint32_t family = family_count;
        for (uint32_t i = 0; i < family_count; ++i) {
            if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
        }
        if (family == family_count) throw std::runtime_error("no compute queue");
        const rocm_port::AgentCaps agent{
            VK_API_VERSION_MAJOR(properties.apiVersion),
            VK_API_VERSION_MINOR(properties.apiVersion),
            VK_API_VERSION_PATCH(properties.apiVersion),
            properties.limits.maxComputeWorkGroupInvocations,
            {properties.limits.maxComputeWorkGroupSize[0],
             properties.limits.maxComputeWorkGroupSize[1],
             properties.limits.maxComputeWorkGroupSize[2]},
            {properties.limits.maxComputeWorkGroupCount[0],
             properties.limits.maxComputeWorkGroupCount[1],
             properties.limits.maxComputeWorkGroupCount[2]},
            true, true};
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.pEnabledFeatures = &features;
        VkDevice device = VK_NULL_HANDLE;
        check(vkCreateDevice(physical, &device_info, nullptr, &device), "create device");
        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, family, 0, &queue);

        Buffer staging = make_buffer(physical, device, compressed_bytes,
                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        Buffer rgba = make_buffer(physical, device, bytes,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        Buffer words = make_buffer(physical, device, bytes,
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        void* mapped = nullptr;
        check(vkMapMemory(device, staging.memory, 0, compressed_bytes, 0, &mapped),
              "map ASTC staging");
        std::memcpy(mapped, astc.data() + 16, compressed_bytes);
        vkUnmapMemory(device, staging.memory);

        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = format;
        image_info.extent = {width, height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImage image = VK_NULL_HANDLE;
        check(vkCreateImage(device, &image_info, nullptr, &image), "create ASTC image");
        VkMemoryRequirements image_requirements{};
        vkGetImageMemoryRequirements(device, image, &image_requirements);
        VkMemoryAllocateInfo image_allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        image_allocate.allocationSize = image_requirements.size;
        image_allocate.memoryTypeIndex = memory_type(
            physical, image_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkDeviceMemory image_memory = VK_NULL_HANDLE;
        check(vkAllocateMemory(device, &image_allocate, nullptr, &image_memory),
              "allocate ASTC image");
        check(vkBindImageMemory(device, image, image_memory, 0), "bind ASTC image");
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        check(vkCreateImageView(device, &view_info, nullptr, &view), "create image view");
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VkSampler sampler = VK_NULL_HANDLE;
        check(vkCreateSampler(device, &sampler_info, nullptr, &sampler), "create sampler");

        std::array<VkDescriptorSetLayoutBinding, 3> bindings{{
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}};
        VkDescriptorSetLayoutCreateInfo set_layout_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set_layout_info.bindingCount = uint32_t(bindings.size());
        set_layout_info.pBindings = bindings.data();
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        check(vkCreateDescriptorSetLayout(device, &set_layout_info, nullptr, &set_layout),
              "create descriptor layout");
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
        VkPipelineLayoutCreateInfo pipeline_layout_info{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &set_layout;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        check(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr,
                                     &pipeline_layout), "create pipeline layout");
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = shader.size();
        module_info.pCode = reinterpret_cast<const uint32_t*>(shader.data());
        VkShaderModule module = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &module_info, nullptr, &module),
              "create shader module");
        VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline_info.layout = pipeline_layout;
        pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_info.stage.module = module;
        pipeline_info.stage.pName = "main";
        VkPipeline pipeline = VK_NULL_HANDLE;
        check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                       nullptr, &pipeline), "create fused pipeline");

        std::array<VkDescriptorPoolSize, 2> pool_sizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}}};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = uint32_t(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        VkDescriptorPool pool = VK_NULL_HANDLE;
        check(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool),
              "create descriptor pool");
        VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        set_info.descriptorPool = pool;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &set_layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        check(vkAllocateDescriptorSets(device, &set_info, &set), "allocate descriptor set");
        VkDescriptorImageInfo sampled{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo rgba_info{rgba.buffer, 0, bytes};
        VkDescriptorBufferInfo words_info{words.buffer, 0, bytes};
        std::array<VkWriteDescriptorSet, 3> writes{};
        for (uint32_t i = 0; i < writes.size(); ++i) {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                               : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pImageInfo = i == 0 ? &sampled : nullptr;
            writes[i].pBufferInfo = i == 1 ? &rgba_info : (i == 2 ? &words_info : nullptr);
        }
        vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);

        VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        command_pool_info.queueFamilyIndex = family;
        VkCommandPool command_pool = VK_NULL_HANDLE;
        check(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool),
              "create command pool");
        VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command_info.commandPool = command_pool;
        command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_info.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device, &command_info, &command),
              "allocate command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(command, &begin), "begin command buffer");
        VkImageMemoryBarrier image_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = image;
        image_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &image_barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(command, staging.buffer, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &image_barrier);

        const rocm_port::KernelSpec kernel{"fusedAstcU64", {32, 1, 1}, 8};
        const rocm_port::DispatchPacket packet{&kernel, {tiles, 1, 1}};
        const uint32_t dimensions[2] = {width, height};
        rocm_port::encode_dispatch(command, agent, packet,
                                   {pipeline, pipeline_layout, set},
                                   dimensions, sizeof(dimensions));
        std::array<VkBufferMemoryBarrier, 2> readback{};
        for (uint32_t i = 0; i < readback.size(); ++i) {
            readback[i] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            readback[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            readback[i].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            readback[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            readback[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            readback[i].buffer = i == 0 ? rgba.buffer : words.buffer;
            readback[i].size = bytes;
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                             uint32_t(readback.size()), readback.data(), 0, nullptr);
        check(vkEndCommandBuffer(command), "end command buffer");
        rocm_port::submit_and_wait(device, queue, command, {1, 1}, 10'000'000'000ULL);

        check(vkMapMemory(device, rgba.memory, 0, bytes, 0, &mapped), "map RGBA");
        const auto* rgba_bytes = static_cast<const uint8_t*>(mapped);
        size_t rgba_mismatches = 0;
        for (size_t i = 0; i < bytes; ++i)
            rgba_mismatches += rgba_bytes[i] != golden[i];
        save_file(argv[4], rgba_bytes, bytes);
        vkUnmapMemory(device, rgba.memory);
        std::printf("ASTC RGBA mismatched bytes=%zu/%zu\n", rgba_mismatches, size_t(bytes));
        if (rgba_mismatches) throw std::runtime_error("ASTC sampled output mismatch");

        std::vector<uint64_t> input_lanes(pixel_count / 2);
        std::vector<uint64_t> expected_lanes(input_lanes.size());
        for (size_t i = 0; i < input_lanes.size(); ++i) {
            const uint32_t low = le32(golden.data() + (2 * i) * 4);
            const uint32_t high = le32(golden.data() + (2 * i + 1) * 4);
            input_lanes[i] = uint64_t(low) | (uint64_t(high) << 32);
        }
        // One vector-length-agnostic SVE traversal replaces 1,024 tile calls.
        sve_xor_shift_one(input_lanes.data(), expected_lanes.data(), input_lanes.size());
        check(vkMapMemory(device, words.memory, 0, bytes, 0, &mapped), "map word planes");
        const auto* actual_words = static_cast<const uint32_t*>(mapped);
        size_t word_mismatches = 0;
        for (uint32_t tile = 0; tile < tiles; ++tile) {
            for (uint32_t lane = 0; lane < 32; ++lane) {
                const uint64_t expected = expected_lanes[size_t(tile) * 32 + lane];
                word_mismatches += actual_words[size_t(tile) * 64 + lane] !=
                                   uint32_t(expected);
                word_mismatches += actual_words[size_t(tile) * 64 + 32 + lane] !=
                                   uint32_t(expected >> 32);
            }
        }
        save_file(argv[5], actual_words, bytes);
        vkUnmapMemory(device, words.memory);
        std::printf("SVE/GPU word-plane mismatches=%zu/%zu\n",
                    word_mismatches, pixel_count);
        if (word_mismatches) throw std::runtime_error("SVE/GPU word-plane mismatch");
        std::printf("PASS: one fused dispatch, %u logical 2048-bit tiles\n", tiles);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ERROR: %s\n", error.what());
        return 1;
    }
}
