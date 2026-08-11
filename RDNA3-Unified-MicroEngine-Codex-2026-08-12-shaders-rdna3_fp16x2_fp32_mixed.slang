// 해당코드는 Codex로 수정됨
struct DispatchInfo
{
    uint elementCount;
};

[[vk::binding(0, 0)]] StructuredBuffer<uint> packedFp16x2Data;
[[vk::binding(1, 0)]] RWStructuredBuffer<float> resultFp32Data;
[[vk::push_constant]] ConstantBuffer<DispatchInfo> dispatchInfo;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint index = dispatchThreadID.x;
    if (index >= dispatchInfo.elementCount)
    {
        return;
    }

    const uint packed = packedFp16x2Data[index];
    const float2 unpackedFp32 = float2(
        f16tof32(packed & 0xffffu),
        f16tof32(packed >> 16u));
    const half2 valuesFp16 = half2(unpackedFp32);
    const half2 squaredFp16 = valuesFp16 * valuesFp16;

    resultFp32Data[index] =
        float(squaredFp16.x) + float(squaredFp16.y);
}
