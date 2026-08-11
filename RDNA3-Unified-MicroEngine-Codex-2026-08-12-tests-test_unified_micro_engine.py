# 해당코드는 Codex로 수정됨
from __future__ import annotations

import pathlib
import struct
import unittest


ROOT = pathlib.Path(__file__).resolve().parent
MASK64 = (1 << 64) - 1


def half(value: float) -> float:
    return struct.unpack("<e", struct.pack("<e", value))[0]


def pack_half2(low: float, high: float) -> int:
    return int.from_bytes(
        struct.pack("<ee", half(low), half(high)), "little")


def unpack_half2(packed: int) -> tuple[float, float]:
    return struct.unpack("<ee", packed.to_bytes(4, "little"))


def signed_byte(value: int) -> int:
    return value - 256 if value & 0x80 else value


def packed_i8x4_dot(lhs: int, rhs: int, bias: int) -> int:
    total = bias
    for lane in range(4):
        total += signed_byte((lhs >> (8 * lane)) & 0xFF) * signed_byte(
            (rhs >> (8 * lane)) & 0xFF
        )
    return total


def distribute_u64(values: list[int], wave_size: int) -> tuple[list[int], list[int]]:
    if not 2 <= len(values) <= 32:
        raise ValueError("lane count must be in [2, 32]")
    if wave_size not in (32, 64):
        raise ValueError("wave size must be 32 or 64")

    transformed = [(value ^ ((value << 1) & MASK64)) & MASK64 for value in values]
    words = [0] * 64
    for lane, value in enumerate(transformed):
        words[lane] = value & 0xFFFFFFFF
        words[lane + 32] = value >> 32
    repacked = [words[lane] | (words[lane + 32] << 32) for lane in range(len(values))]
    return words, repacked


class QueueModel:
    def __init__(self, quantum: int = 2) -> None:
        self.quantum = quantum
        self.remaining = quantum
        self.jobs: list[int] = []


def schedule_model(queues: list[tuple[int, QueueModel]]) -> list[int]:
    output: list[int] = []
    cursors = {priority: 0 for priority in range(4)}
    while any(queue.jobs for _, queue in queues):
        for priority in range(3, -1, -1):
            matching = [index for index, item in enumerate(queues) if item[0] == priority]
            if not matching:
                continue
            start = cursors[priority] % len(matching)
            selected = None
            for offset in range(len(matching)):
                candidate = matching[(start + offset) % len(matching)]
                if queues[candidate][1].jobs:
                    selected = candidate
                    break
            if selected is None:
                continue
            queue = queues[selected][1]
            output.append(queue.jobs.pop(0))
            queue.remaining -= 1
            if queue.remaining == 0 or not queue.jobs:
                queue.remaining = queue.quantum
                cursors[priority] = (matching.index(selected) + 1) % len(matching)
            break
    return output


class NumericReferenceTests(unittest.TestCase):
    def test_pure_fp32(self) -> None:
        values = [-3.0, 0.5, 7.0]
        self.assertEqual([value * value + 2.0 for value in values], [11.0, 2.25, 51.0])

    def test_pure_fp16x2_rounding(self) -> None:
        low, high = unpack_half2(pack_half2(1.125, -2.25))
        result = (
            half(half(low * low) + half(2.0)),
            half(half(high * high) + half(2.0)),
        )
        self.assertEqual(result, (half(3.265625), half(7.0625)))

    def test_fp16x2_fp32_mixed(self) -> None:
        low, high = unpack_half2(pack_half2(1.125, -2.25))
        result = float(half(low * low)) + float(half(high * high))
        self.assertEqual(result, 6.328125)

    def test_int8x4_int32_mixed(self) -> None:
        lhs = int.from_bytes(bytes([1, 0xFE, 3, 0xFC]), "little")
        rhs = int.from_bytes(bytes([5, 6, 0xF9, 8]), "little")
        self.assertEqual(packed_i8x4_dot(lhs, rhs, 11), -49)

    def test_sve_sme_distribution_is_wave_invariant(self) -> None:
        values = [0x0123456789ABCDEF, 0xFEDCBA9876543210, 0x8000000000000001]
        words32, repacked32 = distribute_u64(values, 32)
        words64, repacked64 = distribute_u64(values, 64)
        self.assertEqual(words32, words64)
        self.assertEqual(repacked32, repacked64)


class SchedulerPolicyTests(unittest.TestCase):
    def test_strict_priority_then_quantum_round_robin(self) -> None:
        normal_a = QueueModel(quantum=2)
        normal_b = QueueModel(quantum=2)
        realtime = QueueModel(quantum=1)
        normal_a.jobs = [10, 11, 12]
        normal_b.jobs = [20, 21, 22]
        realtime.jobs = [99]
        order = schedule_model([(1, normal_a), (1, normal_b), (3, realtime)])
        self.assertEqual(order, [99, 10, 11, 20, 21, 12, 22])


class SourceContractTests(unittest.TestCase):
    def test_codex_marker_and_shader_contracts(self) -> None:
        text_files = [
            path
            for path in ROOT.iterdir()
            if path.suffix in {".slang", ".cpp", ".hpp", ".py", ".ps1", ".md", ".txt"}
            or path.name == "CMakeLists.txt"
        ]
        self.assertGreaterEqual(len(text_files), 10)
        for path in text_files:
            self.assertIn("해당코드는 Codex로 수정됨", path.read_text(encoding="utf-8-sig"), path.name)

        int8_shader = (ROOT / "shaders-rdna3_int8x4_int32_mixed.slang").read_text(
            encoding="utf-8"
        )
        self.assertIn("DotProductInput4x8BitPacked", int8_shader)
        self.assertIn("PackedVectorFormat4x8Bit", int8_shader)
        ingress_shader = (ROOT / "shaders-rdna_sve_sme_u64_ingress.slang").read_text(
            encoding="utf-8"
        )
        self.assertEqual(ingress_shader.count("GroupMemoryBarrierWithGroupSync"), 2)

    def test_generated_spirv_contracts_when_present(self) -> None:
        build = ROOT / "build"
        if not build.exists():
            self.skipTest("build directory is not present")
        contracts = {
            "rdna3_pure_fp32_wave.spv-asm": (
                ["; Version: 1.6", "OpTypeFloat 32", "NoContraction"],
                ["OpTypeFloat 16"],
            ),
            "rdna3_pure_fp16x2_wave.spv-asm": (
                ["; Version: 1.6", "OpTypeFloat 16", "OpGroupNonUniformFAdd"],
                ["OpTypeFloat 32"],
            ),
            "rdna3_fp16x2_fp32_mixed.spv-asm": (
                ["OpTypeFloat 16", "OpTypeFloat 32", "OpFConvert", "OpFMul"],
                [],
            ),
            "rdna3_int8x4_int32_mixed.spv-asm": (
                ["DotProductInput4x8BitPacked", "OpSDot", "PackedVectorFormat4x8Bit"],
                ["OpTypeInt 8"],
            ),
            "rdna_sve_sme_u64_ingress.spv-asm": (
                ["; Version: 1.6", "OpControlBarrier"],
                ["OpTypeInt 64"],
            ),
        }
        for filename, (required, forbidden) in contracts.items():
            text = (build / filename).read_text(encoding="utf-8")
            for marker in required:
                self.assertIn(marker, text, filename)
            for marker in forbidden:
                self.assertNotIn(marker, text, filename)


if __name__ == "__main__":
    unittest.main(verbosity=2)
