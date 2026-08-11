// 해당코드는 Codex로 수정됨
// Mixed path: four signed INT8 products plus an INT32 bias/output.
struct DispatchInfo
{
    uint elementCount;
};

[[vk::binding(0, 0)]] StructuredBuffer<uint> lhsPackedInt8x4;
[[vk::binding(1, 0)]] StructuredBuffer<uint> rhsPackedInt8x4;
[[vk::binding(2, 0)]] StructuredBuffer<int> biasInt32;
[[vk::binding(3, 0)]] RWStructuredBuffer<int> resultInt32;
[[vk::push_constant]] ConstantBuffer<DispatchInfo> dispatchInfo;

int dotPackedSignedInt8x4(uint lhs, uint rhs)
{
    return spirv_asm
    {
        OpCapability DotProduct;
        OpCapability DotProductInput4x8BitPacked;
        OpSDot $$int result $lhs $rhs PackedVectorFormat4x8Bit;
    };
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint index = dispatchThreadID.x;
    if (index >= dispatchInfo.elementCount)
    {
        return;
    }

    const int dotInt32 = dotPackedSignedInt8x4(
        lhsPackedInt8x4[index],
        rhsPackedInt8x4[index]);
    resultInt32[index] = dotInt32 + biasInt32[index];
}
