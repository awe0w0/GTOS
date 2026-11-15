#!/usr/bin/env python3
"""Boot the compiled ESP32-P4 image and check GTOS core and PSRAM."""

import argparse
from pathlib import Path
import subprocess


def boot(emulator, build, output, size, expected):
    command = [
        str(emulator), "--chip", "esp32p4",
        "--firmware", str(build / "gtos-a3-flash.bin"),
        "--elf", str(build / "gtos-a3.elf"),
        "--psram-size", size, "--timeout", "20s", "--exit-on", expected,
    ]
    with output.open("xb") as uart:
        subprocess.run(command, stdout=uart, stderr=subprocess.STDOUT, check=True, timeout=30)
    uart = output.read_bytes()
    if expected.encode() not in uart or b"Guru Meditation" in uart or b"PANIC" in uart:
        raise RuntimeError(f"ESP32-P4 did not reach {expected!r}. See {output}")
    print(expected, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    for name in ("cold-start-1", "cold-start-2"):
        boot(args.emulator.absolute(), args.build, args.output / (name + ".log"),
             "16M", "GTOS_A3_CORE PASS psram=16777216 checks=0")
    boot(args.emulator.absolute(), args.build, args.output / "wrong-psram.log",
         "8M", "GTOS_A3_CORE FAIL psram=8388608")


if __name__ == "__main__":
    main()
