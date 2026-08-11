// 해당코드는 Codex로 수정됨
struct DispatchInfo
{
    uint vectorCount;
};

[[vk::binding(0, 0)]] StructuredBuffer<half2> inputData;
[[vk::binding(1, 0)]] RWStructuredBuffer<half2> elementData;
[[vk::binding(2, 0)]] RWStructuredBuffer<half2> subgroupData;
[[vk::push_constant]] ConstantBuffer<DispatchInfo> dispatchInfo;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupID : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint index = dispatchThreadID.x;
    const bool valid = index < dispatchInfo.vectorCount;

    half2 transformed = half2(0.0h, 0.0h);
    if (valid)
    {
        const half2 value = inputData[index];
        const half2 squared = value * value;
        transformed = squared + half2(2.0h, 2.0h);
        elementData[index] = transformed;
    }

    const half2 subgroupSum = WaveActiveSum(transformed);
    if (WaveIsFirstLane())
    {
        const uint waveSize = WaveGetLaneCount();
        const uint subgroupsPerWorkgroup = 64u / waveSize;
        const uint subgroupID = groupIndex / waveSize;
        const uint outputIndex =
            groupID.x * subgroupsPerWorkgroup + subgroupID;
        subgroupData[outputIndex] = subgroupSum;
    }
}
