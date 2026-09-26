// 해당코드는 Codex로 수정됨
// Real GPU integration check for the existing five prebuilt RDNA3 shaders and
// MicroEngineScheduler on the Vulkan 1.3 Xclipse 940 target.
#include "rdna3_micro_engine.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rdna3::micro_engine;

namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " VkResult=" +
                                 std::to_string(result));
}

std::vector<std::uint32_t> readSpirv(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open " + path);
    const auto bytes = file.tellg();
    if (bytes <= 0 || bytes % 4)
        throw std::runtime_error("invalid SPIR-V size: " + path);
    std::vector<std::uint32_t> data(static_cast<std::size_t>(bytes) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), bytes);
    if (!file) throw std::runtime_error("cannot read " + path);
    return data;
}

std::uint32_t memoryType(VkPhysicalDevice physical, std::uint32_t bits) {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    constexpr VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & wanted) == wanted)
            return i;
    throw std::runtime_error("HOST_VISIBLE | HOST_COHERENT memory unavailable");
}

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize bytes = 0;
};

Buffer makeBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize bytes) {
    Buffer buffer{};
    buffer.bytes = bytes;
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = bytes;
    create.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &create, nullptr, &buffer.handle), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType(physical, requirements.memoryTypeBits);
    check(vkAllocateMemory(device, &allocate, nullptr, &buffer.memory), "vkAllocateMemory");
    check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0), "vkBindBufferMemory");
    check(vkMapMemory(device, buffer.memory, 0, bytes, 0, &buffer.mapped), "vkMapMemory");
    std::memset(buffer.mapped, 0, static_cast<std::size_t>(bytes));
    return buffer;
}

void destroyBuffer(VkDevice device, Buffer& buffer) {
    if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
    if (buffer.handle) vkDestroyBuffer(device, buffer.handle, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
}

constexpr std::array<const char*, 5> shaderFiles{
    "rdna_sve_sme_u64_ingress.spv", "rdna3_pure_fp32_wave.spv",
    "rdna3_pure_fp16x2_wave.spv", "rdna3_fp16x2_fp32_mixed.spv",
    "rdna3_int8x4_int32_mixed.spv"};
constexpr std::array<const char*, 5> caseNames{
    "sveSmeU64Ingress", "pureFp32", "pureFp16x2",
    "fp16x2Fp32Mixed", "int8x4Int32Mixed"};
constexpr std::array<std::uint32_t, 5> bindingCounts{3, 3, 3, 2, 4};

std::uint16_t halfInteger(std::uint32_t value) {
    if (!value) return 0;
    std::uint32_t exponent = 0;
    for (std::uint32_t n = value; n > 1; n >>= 1) ++exponent;
    if (exponent > 10) throw std::runtime_error("half test integer too large");
    return static_cast<std::uint16_t>(((exponent + 15) << 10) |
                                      ((value << (10 - exponent)) & 0x3ff));
}

std::uint32_t halfPair(std::uint32_t a, std::uint32_t b) {
    return std::uint32_t(halfInteger(a)) |
           (std::uint32_t(halfInteger(b)) << 16);
}

std::uint32_t packBytes(int a, int b, int c, int d) {
    return std::uint32_t(std::uint8_t(a)) |
           (std::uint32_t(std::uint8_t(b)) << 8) |
           (std::uint32_t(std::uint8_t(c)) << 16) |
           (std::uint32_t(std::uint8_t(d)) << 24);
}

std::vector<VkDeviceSize> sizesFor(std::uint32_t kind) {
    constexpr VkDeviceSize u32x64 = 64 * sizeof(std::uint32_t);
    switch (kind) {
    case 0: return {32 * sizeof(std::uint64_t), u32x64,
                    32 * sizeof(std::uint64_t)};
    case 1: return {u32x64, u32x64, 2 * sizeof(float)};
    case 2: return {u32x64, u32x64, 2 * sizeof(std::uint32_t)};
    case 3: return {u32x64, u32x64};
    case 4: return {u32x64, u32x64, u32x64, u32x64};
    default: throw std::runtime_error("unknown case");
    }
}

void fillInputs(std::uint32_t kind, std::vector<Buffer>& buffers) {
    if (kind == 0) {
        auto* input = static_cast<std::uint64_t*>(buffers[0].mapped);
        for (std::uint32_t i = 0; i < 32; ++i)
            input[i] = 0x0123456789abcdefULL ^
                       (std::uint64_t(i) * 0x0102040810204081ULL);
    } else if (kind == 1) {
        auto* input = static_cast<float*>(buffers[0].mapped);
        for (std::uint32_t i = 0; i < 64; ++i)
            input[i] = static_cast<float>(int(i % 7) - 3);
    } else if (kind == 2 || kind == 3) {
        auto* input = static_cast<std::uint32_t*>(buffers[0].mapped);
        for (std::uint32_t i = 0; i < 64; ++i)
            input[i] = halfPair(i % 3, (i / 3) % 3);
    } else {
        auto* lhs = static_cast<std::uint32_t*>(buffers[0].mapped);
        auto* rhs = static_cast<std::uint32_t*>(buffers[1].mapped);
        auto* bias = static_cast<std::int32_t*>(buffers[2].mapped);
        for (std::uint32_t i = 0; i < 64; ++i) {
            lhs[i] = packBytes(int(i % 5) - 2, -1, 2, int(i % 3));
            rhs[i] = packBytes(2, int(i % 4) - 2, -2, 1);
            bias[i] = int(i % 9) - 4;
        }
    }
}

void verifyOutput(std::uint32_t kind, std::uint32_t wave,
                  const std::vector<Buffer>& buffers) {
    std::size_t mismatches = 0;
    if (kind == 0) {
        const auto* input = static_cast<const std::uint64_t*>(buffers[0].mapped);
        const auto* planes = static_cast<const std::uint32_t*>(buffers[1].mapped);
        const auto* joined = static_cast<const std::uint32_t*>(buffers[2].mapped);
        for (std::uint32_t i = 0; i < 32; ++i) {
            const std::uint64_t expected = input[i] ^ (input[i] << 1);
            mismatches += planes[i] != std::uint32_t(expected);
            mismatches += planes[32 + i] != std::uint32_t(expected >> 32);
            mismatches += joined[2 * i] != std::uint32_t(expected);
            mismatches += joined[2 * i + 1] != std::uint32_t(expected >> 32);
        }
    } else if (kind == 1) {
        const auto* input = static_cast<const float*>(buffers[0].mapped);
        const auto* result = static_cast<const float*>(buffers[1].mapped);
        const auto* sums = static_cast<const float*>(buffers[2].mapped);
        for (std::uint32_t subgroup = 0; subgroup < 64 / wave; ++subgroup) {
            float expectedSum = 0;
            for (std::uint32_t lane = 0; lane < wave; ++lane) {
                const auto i = subgroup * wave + lane;
                const float expected = input[i] * input[i] + 2.0f;
                mismatches += result[i] != expected;
                expectedSum += expected;
            }
            mismatches += sums[subgroup] != expectedSum;
        }
    } else if (kind == 2) {
        const auto* result = static_cast<const std::uint32_t*>(buffers[1].mapped);
        const auto* sums = static_cast<const std::uint32_t*>(buffers[2].mapped);
        for (std::uint32_t subgroup = 0; subgroup < 64 / wave; ++subgroup) {
            std::uint32_t sumA = 0, sumB = 0;
            for (std::uint32_t lane = 0; lane < wave; ++lane) {
                const auto i = subgroup * wave + lane;
                const auto a = i % 3, b = (i / 3) % 3;
                const auto valueA = a * a + 2, valueB = b * b + 2;
                mismatches += result[i] != halfPair(valueA, valueB);
                sumA += valueA;
                sumB += valueB;
            }
            mismatches += sums[subgroup] != halfPair(sumA, sumB);
        }
    } else if (kind == 3) {
        const auto* result = static_cast<const float*>(buffers[1].mapped);
        for (std::uint32_t i = 0; i < 64; ++i) {
            const auto a = i % 3, b = (i / 3) % 3;
            mismatches += result[i] != float(a * a + b * b);
        }
    } else {
        const auto* result = static_cast<const std::int32_t*>(buffers[3].mapped);
        for (std::uint32_t i = 0; i < 64; ++i) {
            const int expected = (int(i % 5) - 2) * 2 +
                                 (-1) * (int(i % 4) - 2) + 2 * (-2) +
                                 int(i % 3) + int(i % 9) - 4;
            mismatches += result[i] != expected;
        }
    }
    std::printf("%s Wave%u mismatches=%zu PASS=%u\n",
                caseNames[kind], wave, mismatches, mismatches == 0);
    std::fflush(stdout);
    if (mismatches) throw std::runtime_error("GPU output differs from CPU reference");
}

void barrier(VkCommandBuffer command, bool before) {
    VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    memory.srcStageMask = before ? VK_PIPELINE_STAGE_2_HOST_BIT
                                 : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memory.srcAccessMask = before ? VK_ACCESS_2_HOST_WRITE_BIT
                                  : VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    memory.dstStageMask = before ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                                 : VK_PIPELINE_STAGE_2_HOST_BIT;
    memory.dstAccessMask = before ? VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                                  : VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &memory;
    vkCmdPipelineBarrier2(command, &dependency);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::fprintf(stderr, "usage: %s /path/to/prebuilt\n", argv[0]);
            return 2;
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "RDNA3 micro-engine Xclipse GPU integration";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &app;
        VkInstance instance = VK_NULL_HANDLE;
        check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");
        std::uint32_t physicalCount = 0;
        check(vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr),
              "vkEnumeratePhysicalDevices");
        if (!physicalCount) throw std::runtime_error("no Vulkan device");
        std::vector<VkPhysicalDevice> devices(physicalCount);
        check(vkEnumeratePhysicalDevices(instance, &physicalCount, devices.data()),
              "vkEnumeratePhysicalDevices(list)");
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties properties{};
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties current{};
            vkGetPhysicalDeviceProperties(candidate, &current);
            if (std::strstr(current.deviceName, "Xclipse 940")) {
                physical = candidate;
                properties = current;
                break;
            }
        }
        if (!physical) throw std::runtime_error("Xclipse 940 Vulkan device not found");
        const auto support = queryUnifiedDeviceSupport(physical);
        requireUnifiedRdna3Support(support);
        std::printf("GPU=%s vendor=0x%04x Vulkan=%u.%u.%u header=%u\n",
                    properties.deviceName, properties.vendorID,
                    VK_API_VERSION_MAJOR(properties.apiVersion),
                    VK_API_VERSION_MINOR(properties.apiVersion),
                    VK_API_VERSION_PATCH(properties.apiVersion), VK_HEADER_VERSION);
        std::printf("RDNA3contract=(Vulkan1.3=%u AMDvendor=%u Xclipse940=%u) "
                    "Wave32=%u Wave64=%u "
                    "FP16=%u packedINT8dot=%u\n", support.vulkan13OrNewer,
                    support.amdVendor, support.xclipse940,
                    support.wave32, support.wave64,
                    support.shaderFloat16, support.packedSignedInt8DotAccelerated);
        std::fflush(stdout);

        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
        std::uint32_t family = familyCount;
        for (std::uint32_t i = 0; i < familyCount; ++i)
            if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
        if (family == familyCount) throw std::runtime_error("no compute queue");

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        RequiredDeviceFeatures required{};
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pNext = required.head();
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        VkDevice device = VK_NULL_HANDLE;
        check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, family, 0, &queue);

        std::array<VkDescriptorSetLayout, 5> setLayouts{};
        std::array<VkPipelineLayout, 5> pipelineLayouts{};
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 10;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
              "vkCreateDescriptorPool");
        VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = family;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool),
              "vkCreateCommandPool");
        VkCommandBufferAllocateInfo commandAllocate{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocate.commandPool = commandPool;
        commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocate.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device, &commandAllocate, &command),
              "vkAllocateCommandBuffers");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");

        {
            MicroEngineScheduler scheduler(device, 2);
            for (std::uint32_t kind = 0; kind < shaderFiles.size(); ++kind) {
                std::vector<VkDescriptorSetLayoutBinding> bindings(bindingCounts[kind]);
                for (std::uint32_t binding = 0; binding < bindings.size(); ++binding)
                    bindings[binding] = {binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
                VkDescriptorSetLayoutCreateInfo layoutInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
                layoutInfo.pBindings = bindings.data();
                check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                                  &setLayouts[kind]),
                      "vkCreateDescriptorSetLayout");
                VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
                VkPipelineLayoutCreateInfo pipelineLayoutInfo{
                    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                pipelineLayoutInfo.setLayoutCount = 1;
                pipelineLayoutInfo.pSetLayouts = &setLayouts[kind];
                pipelineLayoutInfo.pushConstantRangeCount = 1;
                pipelineLayoutInfo.pPushConstantRanges = &pushRange;
                check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                             &pipelineLayouts[kind]),
                      "vkCreatePipelineLayout");
                const auto words = readSpirv(std::string(argv[1]) + "/" + shaderFiles[kind]);
                VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                moduleInfo.codeSize = words.size() * sizeof(std::uint32_t);
                moduleInfo.pCode = words.data();
                VkShaderModule module = VK_NULL_HANDLE;
                check(vkCreateShaderModule(device, &moduleInfo, nullptr, &module),
                      "vkCreateShaderModule");
                scheduler.registerKernel(static_cast<KernelKind>(kind), module,
                                         pipelineLayouts[kind]);
                vkDestroyShaderModule(device, module, nullptr);
            }
            if (!scheduler.queryStatus().healthy)
                throw std::runtime_error("five scheduler kernels did not register");
            scheduler.addQueue({7, QueuePriority::normal, 4});
            std::uint64_t jobId = 1;
            for (const auto wave : {WavePolicy::wave32, WavePolicy::wave64}) {
                const std::uint32_t waveSize = static_cast<std::uint32_t>(wave);
                for (std::uint32_t kind = 0; kind < shaderFiles.size(); ++kind) {
                    const auto sizes = sizesFor(kind);
                    std::vector<Buffer> buffers;
                    for (auto size : sizes) buffers.push_back(makeBuffer(physical, device, size));
                    fillInputs(kind, buffers);
                    VkDescriptorSetAllocateInfo setAllocate{
                        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                    setAllocate.descriptorPool = descriptorPool;
                    setAllocate.descriptorSetCount = 1;
                    setAllocate.pSetLayouts = &setLayouts[kind];
                    VkDescriptorSet set = VK_NULL_HANDLE;
                    check(vkAllocateDescriptorSets(device, &setAllocate, &set),
                          "vkAllocateDescriptorSets");
                    std::vector<VkDescriptorBufferInfo> bufferInfos(buffers.size());
                    std::vector<VkWriteDescriptorSet> writes(buffers.size());
                    for (std::uint32_t binding = 0; binding < buffers.size(); ++binding) {
                        bufferInfos[binding] = {buffers[binding].handle, 0,
                                                buffers[binding].bytes};
                        writes[binding] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                        writes[binding].dstSet = set;
                        writes[binding].dstBinding = binding;
                        writes[binding].descriptorCount = 1;
                        writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        writes[binding].pBufferInfo = &bufferInfos[binding];
                    }
                    vkUpdateDescriptorSets(device,
                        static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
                    DispatchJob job{};
                    job.jobId = jobId++;
                    job.kernel = static_cast<KernelKind>(kind);
                    job.wavePolicy = wave;
                    job.descriptorSet = set;
                    job.pushConstants = kind == 0
                        ? std::array<std::uint32_t, 4>{1, 32, 32, 32}
                        : std::array<std::uint32_t, 4>{64, 0, 0, 0};
                    job.pushConstantBytes = kind == 0 ? 16 : 4;
                    job.groupCountX = 1;
                    job.flags = JobFlags::barrierBefore | JobFlags::barrierAfter;
                    scheduler.enqueue(7, job);
                    check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
                    barrier(command, true);
                    const auto recorded = scheduler.recordBatch(command, 1);
                    if (recorded.size() != 1 || recorded[0].jobId != job.jobId ||
                        recorded[0].selectedWave != wave)
                        throw std::runtime_error("scheduler did not record requested job/wave");
                    barrier(command, false);
                    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
                    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                    submit.commandBufferCount = 1;
                    submit.pCommandBuffers = &command;
                    check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
                    check(vkWaitForFences(device, 1, &fence, VK_TRUE, 10'000'000'000ULL),
                          "vkWaitForFences");
                    check(vkResetFences(device, 1, &fence), "vkResetFences");
                    verifyOutput(kind, waveSize, buffers);
                    for (auto& buffer : buffers) destroyBuffer(device, buffer);
                }
            }
            std::size_t dispatchEvents = 0;
            for (const auto& entry : scheduler.readLog())
                dispatchEvents += entry.event == SchedulerEvent::recordDispatch;
            if (dispatchEvents != 10) throw std::runtime_error("scheduler log lacks 10 dispatches");
            std::printf("PASS: 5 original RDNA3 shaders x Wave32/Wave64 = 10 GPU dispatches; "
                        "CPU outputs and scheduler log match\n");
        }
        check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
        for (std::uint32_t i = 0; i < setLayouts.size(); ++i) {
            vkDestroyPipelineLayout(device, pipelineLayouts[i], nullptr);
            vkDestroyDescriptorSetLayout(device, setLayouts[i], nullptr);
        }
        vkDestroyFence(device, fence, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
