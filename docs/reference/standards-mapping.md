> 해당코드는 Codex로 수정됨

# MES / RDNA3 ISA 대응표

## Micro Engine Scheduler 문서 반영

| 문서 개념 | 이 패키지 구현 |
|---|---|
| Unmapped / mapped & disconnected / mapped & connected | `QueueState`의 세 상태를 그대로 사용 |
| 제한된 hardware queue와 oversubscription | 생성자 `logicalHardwareQueueSlots`; 가득 차면 disconnected victim을 unmap |
| outstanding work가 있는 큐만 connect | `selectReadyQueueLocked()`는 pending job이 있는 비정지 큐만 선택 |
| aggregated doorbell | `enqueue()`와 `notifyWork()`가 `doorbellSequence`를 증가시키고 mapping을 재평가 |
| Real time / Focus / Normal / Idle | `QueuePriority` 네 단계와 strict priority selection |
| 동일 우선순위 round-robin | 우선순위별 cursor와 `quantumDispatches` |
| mid-command-buffer preemption | Vulkan command buffer 내부 dispatch 경계까지만 모델링; 실제 선점은 KMD/MES 책임 |
| suspend / resume / remove | 같은 이름의 public 메서드와 상태 검사 |
| API completion/status | `queryStatus()`, `snapshotQueues()`; 실제 GPU 완료 fence는 호출자가 관리 |
| API / event / interrupt circular history | 256개 고정 원형 `SchedulerLogEntry`; 사용자 공간 API/상태 이벤트만 기록 |

MES API의 64-DWORD packet, MQD/VMID/GDS 주소, trap handler, KMD completion fence는 권한이 있는 커널 드라이버와 펌웨어 사이의 ABI다. 이 패키지는 그 비공개 실행 권한을 흉내 내지 않으며, 애플리케이션에서 의미가 있는 scheduling metadata만 안전하게 재구성한다.

## RDNA3 ISA 문서 반영

| ISA 규칙 | 구현 영향 |
|---|---|
| Wave32와 Wave64 모두 지원; Wave64 VALU/VMEM은 보통 low/high half로 두 번 issue | 모든 shader local size를 64로 통일하고, required subgroup size 32/64 pipeline 두 개를 생성 |
| VOPD는 Wave32에서만 합법 | `automatic`은 Wave32 선택; Wave64에서는 동일 SPIR-V의 비-VOPD lowering을 사용 |
| 16-bit 두 개가 한 32-bit VGPR의 low/high half를 사용 | pure FP16과 mixed FP16 입력을 `half2`/packed 32-bit로 표현 |
| VOP3P packed FP16 명령 | `half2`의 vector FP16 multiply/add가 `V_PK_MUL_F16`, `V_PK_ADD_F16`의 후보가 되도록 유지 |
| `V_DOT4_I32_IU8`는 four byte dot + I32 accumulator | packed uint 두 개를 `OpSDot ... PackedVectorFormat4x8Bit`로 계산하고 INT32 bias를 더함 |
| VOPD 두 op는 독립적이어야 하며 DPP와 결합할 수 없음 | 소스에서 특정 VOPD pairing을 주장하지 않고 최종 AMD ISA dump를 필수 검증 대상으로 둠 |
| work-group barrier는 wave들을 모으지만 outstanding memory counter를 자동으로 기다리지 않음 | shader 내부 LDS는 group barrier 두 번, dispatch 사이 storage 의존성은 `vkCmdPipelineBarrier2`로 분리 |
| LDS는 work-group wave 간 공유 | ingress는 32개의 `uint2` lane과 64개의 `uint` word plane을 groupshared로 배치 |

## SVE/SME 64-bit ingress의 Wave 불변성

shader에서 native 64-bit 정수 타입을 쓰지 않는다. 각 lane은 `uint2(low32, high32)`이고 물리 슬롯은 다음 식으로 정한다.

```text
physicalSlot = subgroupIndex * subgroupSize + subgroupLane
```

- Wave32: subgroup 0이 low plane 0–31, subgroup 1이 high plane 32–63을 맡는다.
- Wave64: 하나의 subgroup에서 low/high half가 같은 64개 슬롯을 맡는다.

따라서 두 wave mode의 output layout은 동일하다. SPIR-V assembly 검사도 `OpTypeInt 64`가 없는지 확인한다.

## 검증 경계

Slang/SPIR-V 검증으로 타입, capability, barrier와 mixed precision 경로는 확인할 수 있다. 다음 항목은 실제 RDNA3 장치에서 별도 확인해야 한다.

- driver가 FP16 vector op를 `V_PK_*_F16`으로 선택했는지
- packed signed dot가 `V_DOT4_I32_IU8`로 선택됐는지
- Wave32 FP32 코드에 합법적인 `V_DUAL_*`가 생겼는지
- LDS 접근 전 `S_WAITCNT`와 `S_BARRIER` 배치가 올바른지
- 실제 occupancy, VGPR/LDS 사용량, queue preemption/quantum 결과
