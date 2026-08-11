> 해당코드는 Codex로 수정됨

# API와 descriptor layout

## 공통 pipeline 조건

- compute entry point: `main`
- local size: 64 × 1 × 1
- pipeline stage flag: `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT`
- `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo::requiredSubgroupSize`: 32 또는 64
- push constant stage: `VK_SHADER_STAGE_COMPUTE_BIT`
- push constant 최대 크기: 16바이트

## Descriptor binding

### `sveSmeU64Ingress`

| binding | type | 의미 |
|---|---|---|
| 0 | storage buffer, read-only, `uint2[]` | 64-bit lane의 low/high word |
| 1 | storage buffer, read-write, `uint[]` | tile마다 64개의 분배 word |
| 2 | storage buffer, read-write, `uint2[]` | 변환 뒤 재결합 lane |

push constant: `{ tileCount, laneCount64, inputStride64, outputStride64 }`. `laneCount64`는 2–32이고 두 stride는 lane count 이상이어야 한다. dispatch X는 `tileCount`다.

### `pureFp32`

| binding | type |
|---|---|
| 0 | read-only `float[]` |
| 1 | read-write element `float[]` |
| 2 | read-write subgroup sum `float[]` |

push constant는 `elementCount`; dispatch X는 `(elementCount + 63) / 64`. subgroup output 수는 dispatch group마다 Wave32에서 2개, Wave64에서 1개다.

### `pureFp16x2`

binding은 FP32 경로와 같지만 원소 타입은 `half2`다. push constant `vectorCount`는 FP16 scalar 수가 아니라 FP16x2 vector 수다. output buffer에도 half2가 저장되며 SPIR-V에 FP32 타입이 없어야 한다.

### `fp16x2Fp32Mixed`

| binding | type |
|---|---|
| 0 | read-only packed FP16x2 `uint[]` |
| 1 | read-write FP32 result `float[]` |

push constant는 `elementCount`. 각 32-bit input의 low/high half를 FP16으로 해석하고, FP16으로 제곱한 뒤 두 결과만 FP32로 승격해 더한다.

### `int8x4Int32Mixed`

| binding | type |
|---|---|
| 0 | read-only packed signed INT8x4 `uint[]` |
| 1 | read-only packed signed INT8x4 `uint[]` |
| 2 | read-only bias `int[]` |
| 3 | read-write result `int[]` |

push constant는 `elementCount`. byte 순서는 little-endian lane 0, 1, 2, 3이다. 수식은 `sum(signed(lhs.byte[i]) * signed(rhs.byte[i])) + bias`다.

## 사용 예시

```cpp
// 해당코드는 Codex로 수정됨
using namespace rdna3::micro_engine;

MicroEngineScheduler scheduler(device, 2, pipelineCache);
scheduler.registerKernel(KernelKind::pureFp32, fp32Module, fp32Layout);
// 나머지 네 module/layout도 같은 방식으로 등록한다.

scheduler.addQueue({.queueId = 7,
                    .priority = QueuePriority::normal,
                    .quantumDispatches = 4});

DispatchJob job{};
job.jobId = 1001;
job.kernel = KernelKind::pureFp32;
job.wavePolicy = WavePolicy::automatic;
job.descriptorSet = fp32Set;
job.pushConstants[0] = elementCount;
job.pushConstantBytes = 4;
job.groupCountX = (elementCount + 63) / 64;
job.flags = JobFlags::barrierBefore | JobFlags::barrierAfter;
scheduler.enqueue(7, job);

const auto recorded = scheduler.recordBatch(commandBuffer, 64);
```

`recordBatch()`는 Vulkan queue에 submit하지 않는다. command buffer begin/end, queue submit, semaphore/fence/timeline 관리는 애플리케이션이 담당한다.

같은 `KernelKind`를 다시 등록할 수 없고, scheduler를 파괴하기 전에는 해당 pipeline을 사용하는 모든 GPU 제출이 완료돼 있어야 한다.
