#!/usr/bin/env python3
"""Boot ESP32-P4 and ESP32-C6 together and test the GTOS radio page."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import time

from verify_board import Guest, prepare_board


def boot(args):
    out, board = prepare_board(args)
    log = out / "c6.log"
    with tempfile.TemporaryDirectory(prefix="gtos-a3-sdio-") as directory:
        bridge = Path(directory) / "bridge.sock"
        slave = [
            str(args.emulator.resolve()), "--chip", "esp32c6",
            "--firmware", str(args.c6 / "gtos-a3-c6-flash.bin"),
            "--elf", str(args.c6 / "gtos-a3-c6.elf"),
            "--hosted", f"bridge:slave:{bridge}", "--net", "user,restrict=on",
            "--wifi-ssid", "A3-LAB", "--timeout", "90s",
        ]
        with log.open("xb") as uart:
            process = subprocess.Popen(slave, stdin=subprocess.DEVNULL, stdout=uart, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 12
            while not (bridge.exists() and b"GTOS_A3_C6 READY" in log.read_bytes()):
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(f"C6 SDIO slave did not start. See {log}")
                time.sleep(0.05)
            host = ["--hosted", f"bridge:host:{bridge}", "--hosted-reset-gpio", "6",
                    "--hosted-reset-active-high", "--net", "user,restrict=on"]
            flash = args.build / "gtos-a3-flash.bin"
            with Guest(args, flash, board, out / "p4-radio.log", extra=host) as vm:
                vm.find("GTOS_A3_READY console=1")
                offset = vm.click("back")
                vm.find("page=2 light=60 awake=1", offset)
                vm.find("GTOS_A3_RADIO SDIO PASS C6 firmware RPC", offset, timeout=25)
                vm.find("ssid=A3-LAB", offset, timeout=30)
                vm.find("GTOS_A3_RADIO BLE PASS advertising", offset, timeout=30)
                vm.find("GTOS_A3_WIRELESS powered=1 sdio=1 wifi=1 ble=1", vm.send("s"))
                vm.frame(out / "radio.png", 614)
                vm.finish()
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    if b"LE_Set_Advertising_Enable = 1" not in log.read_bytes():
        raise RuntimeError(f"C6 virtual controller did not enable advertising. See {log}")
    print("C6 SDIO RPC, Wi-Fi scan, BLE advertising and dashboard passed:", out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--c6", type=Path, required=True)
    parser.add_argument("--parts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    boot(parser.parse_args())


if __name__ == "__main__":
    main()
