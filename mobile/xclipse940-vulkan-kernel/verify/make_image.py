import struct
import zlib
from pathlib import Path

WIDTH = 256
HEIGHT = 256


def chunk(kind: bytes, data: bytes) -> bytes:
    payload = kind + data
    return struct.pack(">I", len(data)) + payload + struct.pack(">I", zlib.crc32(payload))


rows = bytearray()
for y in range(HEIGHT):
    rows.append(0)  # PNG filter: none
    for x in range(WIDTH):
        checker = ((x // 32) ^ (y // 32)) & 1
        r = (x + 64 * checker) & 255
        g = (y + 64 * checker) & 255
        b = ((x ^ y) + 40 * checker) & 255
        rows.extend((r, g, b, 255))

png = bytearray(b"\x89PNG\r\n\x1a\n")
png += chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(rows), 6))
png += chunk(b"IEND", b"")
Path(__file__).with_name("source.png").write_bytes(png)
