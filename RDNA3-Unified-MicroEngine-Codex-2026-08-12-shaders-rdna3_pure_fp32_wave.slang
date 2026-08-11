// 해당코드는 Codex로 수정됨
struct DispatchInfo
{
    uint elementCount;
};

[[vk::binding(0, 0)]] StructuredBuffer<float> inputData;
[[vk::binding(1, 0)]] RWStructuredBuffer<float> elementData;
[[vk::binding(2, 0)]] RWStructuredBuffer<float> subgroupData;
[[vk::push_constant]] ConstantBuffer<DispatchInfo> dispatchInfo;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupID : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint index = dispatchThreadID.x;
    const bool valid = index < dispatchInfo.elementCount;

    float transformed = 0.0;
    if (valid)
    {
        const float value = inputData[index];
        precise float squared = value * value;
        precise float result = squared + 2.0;
        transformed = result;
        elementData[index] = transformed;
    }

    const float subgroupSum = WaveActiveSum(transformed);
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
