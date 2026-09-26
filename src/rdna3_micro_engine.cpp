// 해당코드는 Codex로 수정됨
#include "rdna3_micro_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace rdna3::micro_engine {
namespace {

[[nodiscard]] bool isVulkan14OrNewer(std::uint32_t apiVersion) noexcept {
    const std::uint32_t major = VK_API_VERSION_MAJOR(apiVersion);
    const std::uint32_t minor = VK_API_VERSION_MINOR(apiVersion);
    return major > 1 || (major == 1 && minor >= 4);
}

[[nodiscard]] bool containsFlag(VkFlags value, VkFlags flag) noexcept {
    return (value & flag) == flag;
}

[[nodiscard]] bool supportsRequiredWave(
    std::uint32_t waveSize,
    const VkPhysicalDeviceSubgroupSizeControlProperties& properties) noexcept {
    if (waveSize < properties.minSubgroupSize ||
        waveSize > properties.maxSubgroupSize ||
        kWorkgroupSize % waveSize != 0) {
        return false;
    }
    return kWorkgroupSize / waveSize <=
           properties.maxComputeWorkgroupSubgroups;
}

}  // namespace

RequiredDeviceFeatures::RequiredDeviceFeatures() noexcept {
    float16.pNext = &storage16;
    float16.shaderFloat16 = VK_TRUE;

    storage16.pNext = &subgroupExtended;
    storage16.storageBuffer16BitAccess = VK_TRUE;
    storage16.uniformAndStorageBuffer16BitAccess = VK_TRUE;

    subgroupExtended.pNext = &subgroupSize;
    subgroupExtended.shaderSubgroupExtendedTypes = VK_TRUE;

    subgroupSize.pNext = nullptr;
    subgroupSize.subgroupSizeControl = VK_TRUE;
    subgroupSize.computeFullSubgroups = VK_TRUE;
}

UnifiedDeviceSupport queryUnifiedDeviceSupport(
    VkPhysicalDevice physicalDevice) {
    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::invalid_argument(
            "physicalDevice must not be VK_NULL_HANDLE");
    }

    VkPhysicalDeviceShaderFloat16Int8Features float16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDevice16BitStorageFeatures storage16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures subgroupExtended{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES};
    VkPhysicalDeviceSubgroupSizeControlFeatures subgroupSize{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};

    float16.pNext = &storage16;
    storage16.pNext = &subgroupExtended;
    subgroupExtended.pNext = &subgroupSize;

    VkPhysicalDeviceFeatures2 features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &float16;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

    VkPhysicalDeviceSubgroupProperties subgroupProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceSubgroupSizeControlProperties sizeProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
    VkPhysicalDeviceShaderIntegerDotProductProperties dotProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES};
    subgroupProperties.pNext = &sizeProperties;
    sizeProperties.pNext = &dotProperties;

    VkPhysicalDeviceProperties2 properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &subgroupProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

    UnifiedDeviceSupport support{};
    support.vendorId = properties.properties.vendorID;
    support.deviceId = properties.properties.deviceID;
    std::memcpy(
        support.deviceName.data(),
        properties.properties.deviceName,
        support.deviceName.size());
    support.deviceName.back() = '\0';
    support.vulkan14OrNewer =
        isVulkan14OrNewer(properties.properties.apiVersion);
    support.amdVendor = support.vendorId == kAmdVendorId;
    support.shaderFloat16 = float16.shaderFloat16 == VK_TRUE;
    support.storageBuffer16BitAccess =
        storage16.storageBuffer16BitAccess == VK_TRUE;
    support.uniformAndStorageBuffer16BitAccess =
        storage16.uniformAndStorageBuffer16BitAccess == VK_TRUE;
    support.shaderSubgroupExtendedTypes =
        subgroupExtended.shaderSubgroupExtendedTypes == VK_TRUE;
    support.subgroupSizeControl =
        subgroupSize.subgroupSizeControl == VK_TRUE;
    support.computeFullSubgroups =
        subgroupSize.computeFullSubgroups == VK_TRUE;
    support.computeSubgroups = containsFlag(
        subgroupProperties.supportedStages,
        VK_SHADER_STAGE_COMPUTE_BIT);
    support.subgroupBasic = containsFlag(
        subgroupProperties.supportedOperations,
        VK_SUBGROUP_FEATURE_BASIC_BIT);
    support.subgroupArithmetic = containsFlag(
        subgroupProperties.supportedOperations,
        VK_SUBGROUP_FEATURE_ARITHMETIC_BIT);
    support.requiredSizeForCompute = containsFlag(
        sizeProperties.requiredSubgroupSizeStages,
        VK_SHADER_STAGE_COMPUTE_BIT);
    support.minSubgroupSize = sizeProperties.minSubgroupSize;
    support.maxSubgroupSize = sizeProperties.maxSubgroupSize;
    support.maxComputeWorkgroupSubgroups =
        sizeProperties.maxComputeWorkgroupSubgroups;
    support.packedSignedInt8DotAccelerated =
        dotProperties.integerDotProduct4x8BitPackedSignedAccelerated ==
        VK_TRUE;

    const bool commonWaveRequirements = support.subgroupSizeControl &&
                                        support.computeFullSubgroups &&
                                        support.computeSubgroups &&
                                        support.requiredSizeForCompute;
    support.wave32 = commonWaveRequirements &&
                     supportsRequiredWave(32, sizeProperties);
    support.wave64 = commonWaveRequirements &&
                     supportsRequiredWave(64, sizeProperties);
    return support;
}

void requireUnifiedRdna3Support(const UnifiedDeviceSupport& support) {
    if (!support.vulkan14OrNewer) {
        throw std::runtime_error("Vulkan 1.4 runtime support is required");
    }
    if (!support.amdVendor) {
        throw std::runtime_error("The selected device is not an AMD GPU");
    }
    if (!support.shaderFloat16 || !support.storageBuffer16BitAccess ||
        !support.uniformAndStorageBuffer16BitAccess) {
        throw std::runtime_error(
            "shaderFloat16 and 16-bit storage-buffer access are required");
    }
    if (!support.shaderSubgroupExtendedTypes || !support.subgroupBasic ||
        !support.subgroupArithmetic) {
        throw std::runtime_error(
            "Required FP16 subgroup operations are unavailable");
    }
    if (!support.wave32 || !support.wave64) {
        throw std::runtime_error(
            "Both required compute subgroup sizes 32 and 64 are required");
    }
    if (!support.packedSignedInt8DotAccelerated) {
        throw std::runtime_error(
            "Accelerated packed signed INT8x4 dot product is required");
    }
}

MicroEngineScheduler::MicroEngineScheduler(
    VkDevice device,
    std::uint32_t logicalHardwareQueueSlots,
    VkPipelineCache pipelineCache)
    : device_(device),
      pipelineCache_(pipelineCache),
      logicalHardwareQueueSlots_(logicalHardwareQueueSlots) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::invalid_argument("device must not be VK_NULL_HANDLE");
    }
    if (logicalHardwareQueueSlots_ == 0) {
        throw std::invalid_argument(
            "logicalHardwareQueueSlots must be greater than zero");
    }
}

MicroEngineScheduler::~MicroEngineScheduler() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (KernelPipelines& kernel : kernels_) {
        if (kernel.wave32 != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, kernel.wave32, nullptr);
        }
        if (kernel.wave64 != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, kernel.wave64, nullptr);
        }
        kernel = {};
    }
}

std::size_t MicroEngineScheduler::kernelIndex(KernelKind kind) {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= kKernelCount) {
        throw std::invalid_argument("Unknown KernelKind");
    }
    return index;
}

std::size_t MicroEngineScheduler::priorityIndex(QueuePriority priority) {
    const auto index = static_cast<std::size_t>(priority);
    if (index >= 4) {
        throw std::invalid_argument("Unknown QueuePriority");
    }
    return index;
}

std::uint64_t MicroEngineScheduler::monotonicNowNs() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

void MicroEngineScheduler::registerKernel(
    KernelKind kind,
    VkShaderModule module,
    VkPipelineLayout layout) {
    if (module == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) {
        throw std::invalid_argument(
            "module and layout must not be VK_NULL_HANDLE");
    }

    constexpr std::array<std::uint32_t, 2> waveSizes{32, 64};
    std::array<VkPipelineShaderStageRequiredSubgroupSizeCreateInfo, 2>
        requiredSizes{};
    std::array<VkComputePipelineCreateInfo, 2> createInfos{};

    for (std::size_t index = 0; index < createInfos.size(); ++index) {
        requiredSizes[index].sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
        requiredSizes[index].requiredSubgroupSize = waveSizes[index];

        VkPipelineShaderStageCreateInfo stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.pNext = &requiredSizes[index];
        stage.flags =
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = module;
        stage.pName = "main";

        createInfos[index].sType =
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        createInfos[index].stage = stage;
        createInfos[index].layout = layout;
    }

    std::array<VkPipeline, 2> newPipelines{
        VK_NULL_HANDLE, VK_NULL_HANDLE};
    const VkResult result = vkCreateComputePipelines(
        device_,
        pipelineCache_,
        static_cast<std::uint32_t>(createInfos.size()),
        createInfos.data(),
        nullptr,
        newPipelines.data());
    if (result != VK_SUCCESS) {
        for (VkPipeline pipeline : newPipelines) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device_, pipeline, nullptr);
            }
        }
        throw std::runtime_error(
            "vkCreateComputePipelines failed for Wave32/Wave64");
    }

    std::scoped_lock lock(mutex_);
    KernelPipelines& destination = kernels_[kernelIndex(kind)];
    if (destination.wave32 != VK_NULL_HANDLE ||
        destination.wave64 != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, newPipelines[0], nullptr);
        vkDestroyPipeline(device_, newPipelines[1], nullptr);
        throw std::runtime_error(
            "KernelKind is already registered; recreate the scheduler to replace it");
    }
    destination.layout = layout;
    destination.wave32 = newPipelines[0];
    destination.wave64 = newPipelines[1];
}

void MicroEngineScheduler::addQueue(const QueueCreateInfo& createInfo) {
    if (createInfo.quantumDispatches == 0) {
        throw std::invalid_argument(
            "quantumDispatches must be greater than zero");
    }
    (void)priorityIndex(createInfo.priority);

    const std::uint64_t before = monotonicNowNs();
    std::scoped_lock lock(mutex_);
    if (queues_.contains(createInfo.queueId)) {
        throw std::invalid_argument("queueId is already registered");
    }

    LogicalQueue queue{};
    queue.createInfo = createInfo;
    queue.remainingQuantum = createInfo.quantumDispatches;
    queues_.emplace(createInfo.queueId, std::move(queue));
    queueOrder_.push_back(createInfo.queueId);
    appendLogLocked(
        SchedulerEvent::addQueue,
        createInfo.queueId,
        0,
        QueueState::unmapped,
        WavePolicy::automatic,
        0,
        before,
        monotonicNowNs());
}

void MicroEngineScheduler::removeQueue(
    std::uint32_t queueId,
    bool force) {
    const std::uint64_t before = monotonicNowNs();
    std::scoped_lock lock(mutex_);
    LogicalQueue& queue = requireQueueLocked(queueId);
    if (!force && !queue.pendingJobs.empty()) {
        throw std::runtime_error(
            "Queue still has pending work; pass force=true to remove it");
    }

    if (queue.state == QueueState::mappedConnected) {
        disconnectLocked(queue, queueId);
    }
    if (queue.state != QueueState::unmapped) {
        queue.state = QueueState::unmapped;
        appendLogLocked(
            SchedulerEvent::unmapQueue,
            queueId,
            0,
            queue.state,
            WavePolicy::automatic,
            0,
            before,
            monotonicNowNs());
    }

    queues_.erase(queueId);
    queueOrder_.erase(
        std::remove(queueOrder_.begin(), queueOrder_.end(), queueId),
        queueOrder_.end());
    for (std::size_t& cursor : roundRobinCursor_) {
        cursor = queueOrder_.empty() ? 0 : cursor % queueOrder_.size();
    }
    appendLogLocked(
        SchedulerEvent::removeQueue,
        queueId,
        0,
        QueueState::unmapped,
        WavePolicy::automatic,
        0,
        before,
        monotonicNowNs());
}

void MicroEngineScheduler::suspendQueue(std::uint32_t queueId) {
    const std::uint64_t before = monotonicNowNs();
    std::scoped_lock lock(mutex_);
    LogicalQueue& queue = requireQueueLocked(queueId);
    if (queue.state == QueueState::mappedConnected) {
        disconnectLocked(queue, queueId);
    }
    queue.suspended = true;
    appendLogLocked(
        SchedulerEvent::suspendQueue,
        queueId,
        0,
        queue.state,
        WavePolicy::automatic,
        0,
        before,
        monotonicNowNs());
}

void MicroEngineScheduler::resumeQueue(std::uint32_t queueId) {
    const std::uint64_t before = monotonicNowNs();
    std::scoped_lock lock(mutex_);
    LogicalQueue& queue = requireQueueLocked(queueId);
    queue.suspended = false;
    appendLogLocked(
        SchedulerEvent::resumeQueue,
        queueId,
        0,
        queue.state,
        WavePolicy::automatic,
        0,
        before,
        monotonicNowNs());
    if (!queue.pendingJobs.empty()) {
        ensureMappedLocked(queueId);
    }
}

void MicroEngineScheduler::enqueue(
    std::uint32_t queueId,
    const DispatchJob& job) {
    const std::uint64_t before = monotonicNowNs();
    std::scoped_lock lock(mutex_);
    validateJobLocked(job);
    LogicalQueue& queue = requireQueueLocked(queueId);
    if (!knownJobIds_.insert(job.jobId).second) {
        throw std::invalid_argument("jobId must be globally unique");
    }
    queue.pendingJobs.push_back(job);
    appendLogLocked(
        SchedulerEvent::enqueueJob,
        queueId,
        job.jobId,
        queue.state,
        job.wavePolicy,
        0,
        before,
        monotonicNowNs());

    ++queue.doorbellSequence;
    appendLogLocked(
        SchedulerEvent::doorbell,
        queueId,
        job.jobId,
        queue.state,
        job.wavePolicy,
        0,
        before,
        monotonicNowNs());
    if (!queue.suspended) {
        ensureMappedLocked(queueId);
    }
}

void MicroEngineScheduler::notifyWork(std::uint32_t queueId) {
    const std::uint64_t before = monotonicNowNs();
    std::scoped_lock lock(mutex_);
    LogicalQueue& queue = requireQueueLocked(queueId);
    ++queue.doorbellSequence;
    appendLogLocked(
        SchedulerEvent::doorbell,
        queueId,
        0,
        queue.state,
        WavePolicy::automatic,
        0,
        before,
        monotonicNowNs());
    if (!queue.suspended && !queue.pendingJobs.empty()) {
        ensureMappedLocked(queueId);
    }
}

std::optional<RecordedDispatch> MicroEngineScheduler::recordNext(
    VkCommandBuffer commandBuffer) {
    if (commandBuffer == VK_NULL_HANDLE) {
        throw std::invalid_argument(
            "commandBuffer must not be VK_NULL_HANDLE");
    }

    std::scoped_lock lock(mutex_);
    const std::optional<std::uint32_t> selectedQueue =
        selectReadyQueueLocked();
    if (!selectedQueue.has_value()) {
        return std::nullopt;
    }

    const std::uint32_t queueId = *selectedQueue;
    ensureMappedLocked(queueId);
    LogicalQueue& queue = requireQueueLocked(queueId);
    DispatchJob job = queue.pendingJobs.front();
    const WavePolicy selectedWave = selectWaveLocked(job);
    KernelPipelines& kernel = kernels_[kernelIndex(job.kernel)];
    const VkPipeline pipeline = selectedWave == WavePolicy::wave32
                                    ? kernel.wave32
                                    : kernel.wave64;

    const std::uint64_t before = monotonicNowNs();
    queue.state = QueueState::mappedConnected;
    appendLogLocked(
        SchedulerEvent::connectQueue,
        queueId,
        job.jobId,
        queue.state,
        selectedWave,
        0,
        before,
        monotonicNowNs());

    if (hasFlag(job.flags, JobFlags::barrierBefore)) {
        recordMemoryBarrierLocked(commandBuffer);
    }

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        kernel.layout,
        0,
        1,
        &job.descriptorSet,
        0,
        nullptr);
    if (job.pushConstantBytes != 0) {
        vkCmdPushConstants(
            commandBuffer,
            kernel.layout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            job.pushConstantBytes,
            job.pushConstants.data());
    }
    vkCmdDispatch(
        commandBuffer,
        job.groupCountX,
        job.groupCountY,
        job.groupCountZ);

    if (hasFlag(job.flags, JobFlags::barrierAfter)) {
        recordMemoryBarrierLocked(commandBuffer);
    }

    queue.pendingJobs.pop_front();
    if (queue.remainingQuantum > 0) {
        --queue.remainingQuantum;
    }
    disconnectLocked(queue, queueId, job.jobId);

    const std::size_t priority = priorityIndex(queue.createInfo.priority);
    if (queue.remainingQuantum == 0 || queue.pendingJobs.empty()) {
        queue.remainingQuantum = queue.createInfo.quantumDispatches;
        const auto position = std::find(
            queueOrder_.begin(), queueOrder_.end(), queueId);
        if (position != queueOrder_.end() && !queueOrder_.empty()) {
            roundRobinCursor_[priority] =
                (static_cast<std::size_t>(
                     std::distance(queueOrder_.begin(), position)) +
                 1) %
                queueOrder_.size();
        }
    }

    appendLogLocked(
        SchedulerEvent::recordDispatch,
        queueId,
        job.jobId,
        queue.state,
        selectedWave,
        0,
        before,
        monotonicNowNs());
    return RecordedDispatch{
        queueId, job.jobId, job.kernel, selectedWave};
}

std::vector<RecordedDispatch> MicroEngineScheduler::recordBatch(
    VkCommandBuffer commandBuffer,
    std::uint32_t maxDispatches) {
    std::vector<RecordedDispatch> recorded;
    recorded.reserve(maxDispatches);
    for (std::uint32_t index = 0; index < maxDispatches; ++index) {
        std::optional<RecordedDispatch> next = recordNext(commandBuffer);
        if (!next.has_value()) {
            break;
        }
        recorded.push_back(*next);
    }
    return recorded;
}

SchedulerStatus MicroEngineScheduler::queryStatus() const {
    std::scoped_lock lock(mutex_);
    SchedulerStatus status{};
    status.logicalQueueCount = static_cast<std::uint32_t>(queues_.size());
    for (const auto& [queueId, queue] : queues_) {
        static_cast<void>(queueId);
        status.pendingJobCount += queue.pendingJobs.size();
        status.mappedQueueCount +=
            queue.state != QueueState::unmapped ? 1u : 0u;
        status.connectedQueueCount +=
            queue.state == QueueState::mappedConnected ? 1u : 0u;
        status.suspendedQueueCount += queue.suspended ? 1u : 0u;
    }
    for (const KernelPipelines& kernel : kernels_) {
        status.registeredKernelCount +=
            kernel.wave32 != VK_NULL_HANDLE &&
                    kernel.wave64 != VK_NULL_HANDLE
                ? 1u
                : 0u;
    }
    status.healthy = device_ != VK_NULL_HANDLE &&
                     status.registeredKernelCount == kKernelCount;
    status.nextLogSequence = nextLogSequence_;
    return status;
}

std::vector<QueueSnapshot> MicroEngineScheduler::snapshotQueues() const {
    std::scoped_lock lock(mutex_);
    std::vector<QueueSnapshot> snapshots;
    snapshots.reserve(queueOrder_.size());
    for (std::uint32_t queueId : queueOrder_) {
        const LogicalQueue& queue = requireQueueLocked(queueId);
        snapshots.push_back(QueueSnapshot{
            queueId,
            queue.createInfo.priority,
            queue.state,
            queue.suspended,
            queue.createInfo.quantumDispatches,
            queue.remainingQuantum,
            queue.doorbellSequence,
            queue.pendingJobs.size()});
    }
    return snapshots;
}

std::vector<SchedulerLogEntry> MicroEngineScheduler::readLog() const {
    std::scoped_lock lock(mutex_);
    std::vector<SchedulerLogEntry> entries;
    entries.reserve(logSize_);
    const std::size_t first =
        (logHead_ + kSchedulerLogCapacity - logSize_) %
        kSchedulerLogCapacity;
    for (std::size_t index = 0; index < logSize_; ++index) {
        entries.push_back(
            log_[(first + index) % kSchedulerLogCapacity]);
    }
    return entries;
}

MicroEngineScheduler::LogicalQueue&
MicroEngineScheduler::requireQueueLocked(std::uint32_t queueId) {
    const auto iterator = queues_.find(queueId);
    if (iterator == queues_.end()) {
        throw std::invalid_argument("Unknown queueId");
    }
    return iterator->second;
}

const MicroEngineScheduler::LogicalQueue&
MicroEngineScheduler::requireQueueLocked(std::uint32_t queueId) const {
    const auto iterator = queues_.find(queueId);
    if (iterator == queues_.end()) {
        throw std::invalid_argument("Unknown queueId");
    }
    return iterator->second;
}

WavePolicy MicroEngineScheduler::selectWaveLocked(
    const DispatchJob& job) const {
    if (job.wavePolicy == WavePolicy::automatic) {
        // RDNA3 VOPD is Wave32-only. Wave64 remains an explicit supported path.
        return WavePolicy::wave32;
    }
    if (job.wavePolicy != WavePolicy::wave32 &&
        job.wavePolicy != WavePolicy::wave64) {
        throw std::invalid_argument("Unknown WavePolicy");
    }
    return job.wavePolicy;
}

std::optional<std::uint32_t>
MicroEngineScheduler::selectReadyQueueLocked() {
    if (queueOrder_.empty()) {
        return std::nullopt;
    }

    for (int priority = 3; priority >= 0; --priority) {
        const std::size_t start =
            roundRobinCursor_[static_cast<std::size_t>(priority)] %
            queueOrder_.size();
        for (std::size_t offset = 0; offset < queueOrder_.size(); ++offset) {
            const std::size_t position =
                (start + offset) % queueOrder_.size();
            const std::uint32_t queueId = queueOrder_[position];
            LogicalQueue& queue = requireQueueLocked(queueId);
            if (!queue.suspended && !queue.pendingJobs.empty() &&
                static_cast<int>(queue.createInfo.priority) == priority) {
                return queueId;
            }
        }
    }
    return std::nullopt;
}

void MicroEngineScheduler::ensureMappedLocked(std::uint32_t queueId) {
    LogicalQueue& queue = requireQueueLocked(queueId);
    if (queue.state != QueueState::unmapped) {
        return;
    }

    const auto mappedCount = static_cast<std::uint32_t>(std::count_if(
        queues_.begin(),
        queues_.end(),
        [](const auto& item) {
            return item.second.state != QueueState::unmapped;
        }));
    if (mappedCount >= logicalHardwareQueueSlots_) {
        unmapVictimLocked(queueId);
    }

    queue.state = QueueState::mappedDisconnected;
    appendLogLocked(
        SchedulerEvent::mapQueue,
        queueId,
        queue.pendingJobs.empty() ? 0 : queue.pendingJobs.front().jobId,
        queue.state,
        WavePolicy::automatic,
        0,
        monotonicNowNs(),
        monotonicNowNs());
}

void MicroEngineScheduler::unmapVictimLocked(
    std::uint32_t exceptQueueId) {
    auto victim = queues_.end();
    for (auto iterator = queues_.begin(); iterator != queues_.end(); ++iterator) {
        if (iterator->first == exceptQueueId ||
            iterator->second.state != QueueState::mappedDisconnected) {
            continue;
        }
        if (iterator->second.pendingJobs.empty()) {
            victim = iterator;
            break;
        }
        if (victim == queues_.end() ||
            static_cast<std::uint32_t>(iterator->second.createInfo.priority) <
                static_cast<std::uint32_t>(
                    victim->second.createInfo.priority)) {
            victim = iterator;
        }
    }
    if (victim == queues_.end()) {
        throw std::runtime_error(
            "No disconnected logical hardware queue can be unmapped");
    }

    victim->second.state = QueueState::unmapped;
    appendLogLocked(
        SchedulerEvent::unmapQueue,
        victim->first,
        victim->second.pendingJobs.empty()
            ? 0
            : victim->second.pendingJobs.front().jobId,
        victim->second.state,
        WavePolicy::automatic,
        0,
        monotonicNowNs(),
        monotonicNowNs());
}

void MicroEngineScheduler::disconnectLocked(
    LogicalQueue& queue,
    std::uint32_t queueId,
    std::uint64_t jobId) {
    if (queue.state != QueueState::mappedConnected) {
        return;
    }
    queue.state = QueueState::mappedDisconnected;
    appendLogLocked(
        SchedulerEvent::disconnectQueue,
        queueId,
        jobId != 0
            ? jobId
            : (queue.pendingJobs.empty() ? 0 : queue.pendingJobs.front().jobId),
        queue.state,
        WavePolicy::automatic,
        0,
        monotonicNowNs(),
        monotonicNowNs());
}

void MicroEngineScheduler::recordMemoryBarrierLocked(
    VkCommandBuffer commandBuffer) const {
    VkMemoryBarrier2 memoryBarrier{
        VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    memoryBarrier.srcStageMask =
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    memoryBarrier.dstStageMask =
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.dstAccessMask =
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

    VkDependencyInfo dependencyInfo{
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &memoryBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void MicroEngineScheduler::appendLogLocked(
    SchedulerEvent event,
    std::uint32_t queueId,
    std::uint64_t jobId,
    QueueState state,
    WavePolicy wave,
    std::int32_t status,
    std::uint64_t before,
    std::uint64_t after) {
    log_[logHead_] = SchedulerLogEntry{
        nextLogSequence_++,
        event,
        queueId,
        jobId,
        state,
        wave,
        status,
        before,
        after};
    logHead_ = (logHead_ + 1) % kSchedulerLogCapacity;
    logSize_ = std::min(logSize_ + 1, kSchedulerLogCapacity);
}

void MicroEngineScheduler::validateJobLocked(
    const DispatchJob& job) const {
    if (job.jobId == 0) {
        throw std::invalid_argument("jobId must be non-zero");
    }
    const KernelPipelines& kernel = kernels_[kernelIndex(job.kernel)];
    if (kernel.layout == VK_NULL_HANDLE ||
        kernel.wave32 == VK_NULL_HANDLE ||
        kernel.wave64 == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "The requested kernel has not been registered");
    }
    if (job.descriptorSet == VK_NULL_HANDLE) {
        throw std::invalid_argument(
            "descriptorSet must not be VK_NULL_HANDLE");
    }
    if (job.pushConstantBytes > sizeof(job.pushConstants) ||
        job.pushConstantBytes % 4 != 0) {
        throw std::invalid_argument(
            "pushConstantBytes must be a DWORD multiple in [0, 16]");
    }
    if (job.groupCountX == 0 || job.groupCountY == 0 ||
        job.groupCountZ == 0) {
        throw std::invalid_argument(
            "All dispatch group counts must be greater than zero");
    }
    if (job.wavePolicy != WavePolicy::automatic &&
        job.wavePolicy != WavePolicy::wave32 &&
        job.wavePolicy != WavePolicy::wave64) {
        throw std::invalid_argument("Unknown WavePolicy");
    }
}

}  // namespace rdna3::micro_engine
