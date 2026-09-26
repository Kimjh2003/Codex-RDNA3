// 해당코드는 Codex로 수정됨
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan.h>

static_assert(
    VK_HEADER_VERSION >= 304,
    "Vulkan SDK header revision 304 or newer is required");

namespace rdna3::micro_engine {

inline constexpr std::uint32_t kAmdVendorId = 0x1002;
inline constexpr std::uint32_t kSamsungVendorId = 0x144d;
inline constexpr std::uint32_t kWorkgroupSize = 64;
inline constexpr std::size_t kKernelCount = 5;
inline constexpr std::size_t kSchedulerLogCapacity = 256;

enum class KernelKind : std::uint32_t {
    sveSmeU64Ingress = 0,
    pureFp32 = 1,
    pureFp16x2 = 2,
    fp16x2Fp32Mixed = 3,
    int8x4Int32Mixed = 4,
};

enum class WavePolicy : std::uint32_t {
    automatic = 0,
    wave32 = 32,
    wave64 = 64,
};

enum class QueuePriority : std::uint32_t {
    idle = 0,
    normal = 1,
    focus = 2,
    realtime = 3,
};

enum class QueueState : std::uint32_t {
    unmapped = 0,
    mappedDisconnected = 1,
    mappedConnected = 2,
};

enum class JobFlags : std::uint32_t {
    none = 0,
    barrierBefore = 1u << 0,
    barrierAfter = 1u << 1,
};

constexpr JobFlags operator|(JobFlags lhs, JobFlags rhs) noexcept {
    return static_cast<JobFlags>(
        static_cast<std::uint32_t>(lhs) |
        static_cast<std::uint32_t>(rhs));
}

constexpr bool hasFlag(JobFlags value, JobFlags flag) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(flag)) != 0;
}

enum class SchedulerEvent : std::uint32_t {
    addQueue,
    removeQueue,
    mapQueue,
    unmapQueue,
    connectQueue,
    disconnectQueue,
    doorbell,
    enqueueJob,
    recordDispatch,
    suspendQueue,
    resumeQueue,
    error,
};

struct UnifiedDeviceSupport {
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::array<char, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE> deviceName{};
    bool vulkan13OrNewer = false;
    bool amdVendor = false;
    bool xclipse940 = false;
    bool shaderInt16 = false;
    bool shaderFloat16 = false;
    bool storageBuffer16BitAccess = false;
    bool uniformAndStorageBuffer16BitAccess = false;
    bool shaderSubgroupExtendedTypes = false;
    bool subgroupSizeControl = false;
    bool computeFullSubgroups = false;
    bool computeSubgroups = false;
    bool subgroupBasic = false;
    bool subgroupArithmetic = false;
    bool requiredSizeForCompute = false;
    bool wave32 = false;
    bool wave64 = false;
    bool packedSignedInt8DotAccelerated = false;
    bool shaderIntegerDotProduct = false;
    bool synchronization2 = false;
    std::uint32_t minSubgroupSize = 0;
    std::uint32_t maxSubgroupSize = 0;
    std::uint32_t maxComputeWorkgroupSubgroups = 0;
};

// Keep this pNext chain alive until vkCreateDevice returns.
struct RequiredDeviceFeatures {
    VkPhysicalDeviceFeatures2 core{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceShaderFloat16Int8Features float16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDevice16BitStorageFeatures storage16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures subgroupExtended{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES};
    VkPhysicalDeviceSubgroupSizeControlFeatures subgroupSize{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
    VkPhysicalDeviceSynchronization2Features synchronization2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    VkPhysicalDeviceShaderIntegerDotProductFeatures integerDot{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES};

    RequiredDeviceFeatures() noexcept;
    RequiredDeviceFeatures(const RequiredDeviceFeatures&) = delete;
    RequiredDeviceFeatures& operator=(const RequiredDeviceFeatures&) = delete;
    RequiredDeviceFeatures(RequiredDeviceFeatures&&) = delete;
    RequiredDeviceFeatures& operator=(RequiredDeviceFeatures&&) = delete;

    [[nodiscard]] const void* head() const noexcept { return &core; }
};

struct QueueCreateInfo {
    std::uint32_t queueId = 0;
    QueuePriority priority = QueuePriority::normal;
    std::uint32_t quantumDispatches = 4;
};

struct DispatchJob {
    std::uint64_t jobId = 0;
    KernelKind kernel = KernelKind::pureFp32;
    WavePolicy wavePolicy = WavePolicy::automatic;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    std::array<std::uint32_t, 4> pushConstants{};
    std::uint32_t pushConstantBytes = 0;
    std::uint32_t groupCountX = 1;
    std::uint32_t groupCountY = 1;
    std::uint32_t groupCountZ = 1;
    JobFlags flags = JobFlags::barrierBefore;
};

struct RecordedDispatch {
    std::uint32_t queueId = 0;
    std::uint64_t jobId = 0;
    KernelKind kernel = KernelKind::pureFp32;
    WavePolicy selectedWave = WavePolicy::wave32;
};

struct QueueSnapshot {
    std::uint32_t queueId = 0;
    QueuePriority priority = QueuePriority::normal;
    QueueState state = QueueState::unmapped;
    bool suspended = false;
    std::uint32_t quantumDispatches = 0;
    std::uint32_t remainingQuantum = 0;
    std::uint64_t doorbellSequence = 0;
    std::size_t pendingJobs = 0;
};

struct SchedulerStatus {
    bool healthy = false;
    std::uint32_t logicalQueueCount = 0;
    std::uint32_t mappedQueueCount = 0;
    std::uint32_t connectedQueueCount = 0;
    std::uint32_t suspendedQueueCount = 0;
    std::uint32_t registeredKernelCount = 0;
    std::uint64_t pendingJobCount = 0;
    std::uint64_t nextLogSequence = 0;
};

struct SchedulerLogEntry {
    std::uint64_t sequence = 0;
    SchedulerEvent event = SchedulerEvent::error;
    std::uint32_t queueId = 0;
    std::uint64_t jobId = 0;
    QueueState queueState = QueueState::unmapped;
    WavePolicy selectedWave = WavePolicy::automatic;
    std::int32_t status = 0;
    std::uint64_t timeBeforeNs = 0;
    std::uint64_t timeAfterNs = 0;
};

[[nodiscard]] UnifiedDeviceSupport queryUnifiedDeviceSupport(
    VkPhysicalDevice physicalDevice);

// Throws unless the Vulkan 1.3 feature contract is met on AMD or Xclipse 940.
void requireUnifiedRdna3Support(const UnifiedDeviceSupport& support);

class MicroEngineScheduler final {
public:
    explicit MicroEngineScheduler(
        VkDevice device,
        std::uint32_t logicalHardwareQueueSlots = 2,
        VkPipelineCache pipelineCache = VK_NULL_HANDLE);
    ~MicroEngineScheduler();

    MicroEngineScheduler(const MicroEngineScheduler&) = delete;
    MicroEngineScheduler& operator=(const MicroEngineScheduler&) = delete;
    MicroEngineScheduler(MicroEngineScheduler&&) = delete;
    MicroEngineScheduler& operator=(MicroEngineScheduler&&) = delete;

    // The module must contain a compute entry point named "main" with
    // LocalSize 64. Both required-subgroup-size pipelines are created.
    void registerKernel(
        KernelKind kind,
        VkShaderModule module,
        VkPipelineLayout layout);

    void addQueue(const QueueCreateInfo& createInfo);
    void removeQueue(std::uint32_t queueId, bool force = false);
    void suspendQueue(std::uint32_t queueId);
    void resumeQueue(std::uint32_t queueId);

    // Enqueue also rings the scheduler's logical aggregated doorbell.
    void enqueue(std::uint32_t queueId, const DispatchJob& job);
    void notifyWork(std::uint32_t queueId);

    [[nodiscard]] std::optional<RecordedDispatch> recordNext(
        VkCommandBuffer commandBuffer);

    [[nodiscard]] std::vector<RecordedDispatch> recordBatch(
        VkCommandBuffer commandBuffer,
        std::uint32_t maxDispatches);

    [[nodiscard]] SchedulerStatus queryStatus() const;
    [[nodiscard]] std::vector<QueueSnapshot> snapshotQueues() const;
    [[nodiscard]] std::vector<SchedulerLogEntry> readLog() const;

private:
    struct KernelPipelines {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline wave32 = VK_NULL_HANDLE;
        VkPipeline wave64 = VK_NULL_HANDLE;
    };

    struct LogicalQueue {
        QueueCreateInfo createInfo{};
        QueueState state = QueueState::unmapped;
        bool suspended = false;
        std::uint32_t remainingQuantum = 0;
        std::uint64_t doorbellSequence = 0;
        std::deque<DispatchJob> pendingJobs;
    };

    [[nodiscard]] static std::size_t kernelIndex(KernelKind kind);
    [[nodiscard]] static std::size_t priorityIndex(QueuePriority priority);
    [[nodiscard]] static std::uint64_t monotonicNowNs() noexcept;

    [[nodiscard]] LogicalQueue& requireQueueLocked(std::uint32_t queueId);
    [[nodiscard]] const LogicalQueue& requireQueueLocked(
        std::uint32_t queueId) const;
    [[nodiscard]] WavePolicy selectWaveLocked(const DispatchJob& job) const;
    [[nodiscard]] std::optional<std::uint32_t> selectReadyQueueLocked();
    void ensureMappedLocked(std::uint32_t queueId);
    void unmapVictimLocked(std::uint32_t exceptQueueId);
    void disconnectLocked(
        LogicalQueue& queue,
        std::uint32_t queueId,
        std::uint64_t jobId = 0);
    void recordMemoryBarrierLocked(VkCommandBuffer commandBuffer) const;
    void appendLogLocked(
        SchedulerEvent event,
        std::uint32_t queueId,
        std::uint64_t jobId,
        QueueState state,
        WavePolicy wave,
        std::int32_t status,
        std::uint64_t before,
        std::uint64_t after);
    void validateJobLocked(const DispatchJob& job) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    std::uint32_t logicalHardwareQueueSlots_ = 0;
    std::array<KernelPipelines, kKernelCount> kernels_{};
    std::unordered_map<std::uint32_t, LogicalQueue> queues_;
    std::unordered_set<std::uint64_t> knownJobIds_;
    std::vector<std::uint32_t> queueOrder_;
    std::array<std::size_t, 4> roundRobinCursor_{};
    std::array<SchedulerLogEntry, kSchedulerLogCapacity> log_{};
    std::size_t logHead_ = 0;
    std::size_t logSize_ = 0;
    std::uint64_t nextLogSequence_ = 1;
    mutable std::mutex mutex_;
};

}  // namespace rdna3::micro_engine
