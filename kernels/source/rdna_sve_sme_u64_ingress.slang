// 해당코드는 Codex로 수정됨
struct DispatchInfo
{
    uint tileCount;
    uint laneCount64;
    uint inputStride64;
    uint outputStride64;
};

[[vk::binding(0, 0)]] StructuredBuffer<uint2> inputLane64;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> distributedWord32;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint2> outputLane64;
[[vk::push_constant]] ConstantBuffer<DispatchInfo> dispatchInfo;

groupshared uint2 sharedLane64[32];
groupshared uint sharedWordPlane32[64];

uint2 shiftLeftOne64(uint2 value)
{
    return uint2(
        value.x << 1,
        (value.y << 1) | (value.x >> 31));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 groupID : SV_GroupID)
{
    const uint tileIndex = groupID.x;
    const uint laneCount = dispatchInfo.laneCount64;

    if (tileIndex >= dispatchInfo.tileCount ||
        laneCount < 2 || laneCount > 32 ||
        dispatchInfo.inputStride64 < laneCount ||
        dispatchInfo.outputStride64 < laneCount)
    {
        return;
    }

    const uint waveSize = WaveGetLaneCount();
    const uint waveIndex = WaveGetWaveIndex();
    const uint waveLane = WaveGetLaneIndex();
    const uint physicalSlot = waveIndex * waveSize + waveLane;

    if (physicalSlot < 32)
    {
        const uint logicalLane = physicalSlot;
        const uint inputIndex =
            tileIndex * dispatchInfo.inputStride64 + logicalLane;
        sharedLane64[logicalLane] = logicalLane < laneCount
            ? inputLane64[inputIndex]
            : uint2(0u, 0u);
    }

    GroupMemoryBarrierWithGroupSync();

    const uint logicalLane = physicalSlot & 31u;
    const uint wordPlane = physicalSlot >> 5;
    const uint2 rawLane = sharedLane64[logicalLane];
    const uint2 transformedLane = rawLane ^ shiftLeftOne64(rawLane);
    const uint selectedWord = wordPlane == 0u
        ? transformedLane.x
        : transformedLane.y;

    sharedWordPlane32[physicalSlot] =
        logicalLane < laneCount ? selectedWord : 0u;

    GroupMemoryBarrierWithGroupSync();

    distributedWord32[tileIndex * 64u + physicalSlot] =
        sharedWordPlane32[physicalSlot];

    if (physicalSlot < laneCount)
    {
        const uint outputIndex =
            tileIndex * dispatchInfo.outputStride64 + physicalSlot;
        outputLane64[outputIndex] = uint2(
            sharedWordPlane32[physicalSlot],
            sharedWordPlane32[physicalSlot + 32u]);
    }
}
