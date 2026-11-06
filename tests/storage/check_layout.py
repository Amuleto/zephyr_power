"""Check physical partition bounds against linked MCUboot and signed app HEX files."""

import argparse
import re
from pathlib import Path

from intelhex import IntelHex


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, help="sysbuild root directory")
    args = parser.parse_args()
    firmware = Path(__file__).resolve().parents[2]
    dts = firmware / "boards/custom/gd32f303rc/partitions.dtsi"
    matches = re.findall(
        r"(\w+_partition):\s*partition@[0-9a-f]+\s*\{[^}]*?"
        r"reg\s*=\s*<(0x[0-9a-f]+)\s+(0x[0-9a-f]+)>;",
        dts.read_text(encoding="utf-8-sig"),
        re.S,
    )
    regions = {name: (int(start, 16), int(size, 16)) for name, start, size in matches}
    assert len(regions) == 5, "Expected five partitions"
    end = 0
    for name, (start, size) in sorted(regions.items(), key=lambda item: item[1][0]):
        assert start == end, f"Gap or overlap before {name}"
        assert start % 2048 == 0 and size % 2048 == 0, f"Unaligned {name}"
        end = start + size
    assert end == 256 * 1024, "Partitions must cover exactly 256 KiB"
    assert regions["slot1_partition"][1] == regions["slot0_partition"][1] + 2048
    assert regions["storage_partition"][1] >= 2 * 2048, "NVS needs at least two pages"

    for name, image in (
        ("boot_partition", "mcuboot/zephyr/zephyr.hex"),
        ("slot0_partition", "bringup/zephyr/zephyr.signed.hex"),
    ):
        start, size = regions[name]
        low = 0x08000000 + start
        high = low + size
        data = IntelHex(str(args.build_dir / image))
        assert data.minaddr() == low, f"Incorrect start address for {image}"
        assert data.maxaddr() < high, f"Image exceeds {name}"
        print(f"{image}: 0x{data.minaddr():08x}-0x{data.maxaddr():08x}, inside {name}")

    print("PASS: contiguous, aligned 256 KiB layout; linked images stay within their partitions")


if __name__ == "__main__":
    main()
