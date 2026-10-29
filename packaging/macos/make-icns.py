#!/usr/bin/env python3
"""Собрать AutoQ.icns из PNG (mac-0016.png … mac-1024.png).

PNG рисует сам AutoQ:  QT_QPA_PLATFORM=offscreen build/tests/test_icons --mac-icon-dir <папка>
Запуск:               python3 packaging/macos/make-icns.py <папка> packaging/macos/AutoQ.icns
"""
import struct
import sys
from pathlib import Path

# тип записи icns -> размер PNG в пикселях
ENTRIES = [
    ("icp4", 16), ("icp5", 32), ("ic11", 32), ("icp6", 64), ("ic12", 64),
    ("ic07", 128), ("ic13", 256), ("ic08", 256), ("ic14", 512), ("ic09", 512), ("ic10", 1024),
]


def main() -> None:
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    body = b""
    for kind, size in ENTRIES:
        png = (src / f"mac-{size:04d}.png").read_bytes()
        body += kind.encode("ascii") + struct.pack(">I", len(png) + 8) + png
    out.write_bytes(b"icns" + struct.pack(">I", len(body) + 8) + body)
    print(f"{out}: {len(body) + 8} байт, {len(ENTRIES)} размеров")


if __name__ == "__main__":
    main()
