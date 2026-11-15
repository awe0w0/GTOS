#!/usr/bin/env python3
"""Boot ESP32-P4 and ESP32-C6 together and test the GTOS radio page."""

import argparse
from http.client import HTTPConnection, HTTPException
import json
from pathlib import Path
import subprocess
import tempfile
import time

from verify_board import Guest, available_port, prepare_board


def read_status(port, target):
    deadline = time.monotonic() + 8
    while True:
        connection = HTTPConnection("127.0.0.1", port, timeout=2)
        try:
            connection.request("GET", "/status")
            response = connection.getresponse()
            if response.status != 200 or response.getheader("Content-Type") != "application/json":
                raise RuntimeError(f"Unexpected Orbit HTTP response: {response.status} {response.headers}")
            body = response.read()
            break
        except (OSError, HTTPException) as error:
            if time.monotonic() >= deadline:
                raise RuntimeError("No P4 response via the C6 WLAN and SDIO data path") from error
            time.sleep(0.05)
        finally:
            connection.close()
    target.write_bytes(body)
    return json.loads(body)


def expect_status(port, target, expected):
    status = read_status(port, target)
    if any(status.get(key) != value for key, value in expected.items()):
        raise RuntimeError(f"Expected {expected} from the running Orbit application: {target}")


def boot(args, out, board, network, hci_port=None, check_guest=None):
    log = out / "c6.log"
    port = available_port()
    with tempfile.TemporaryDirectory(prefix="gtos-a3-sdio-") as directory:
        bridge = Path(directory) / "bridge.sock"
        slave = [
            str(args.emulator.resolve()), "--chip", "esp32c6",
            "--firmware", str(args.c6 / "gtos-a3-c6-flash.bin"),
            "--elf", str(args.c6 / "gtos-a3-c6.elf"),
            "--hosted", f"bridge:slave:{bridge}",
            "--net", f"user,restrict=on,hostfwd=tcp:127.0.0.1:{port}-:8080",
            "--wifi-ssid", network, "--wifi-password", "orbit-lab-test",
            "--wifi-auth", "wpa2-psk", "--timeout", "90s",
        ]
        if hci_port is not None:
            slave += ["--ble-hci", f"tcp:127.0.0.1:{hci_port}"]
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
                vm.find(f"ssid={network}", offset, timeout=30)
                vm.find("GTOS_A3_RADIO BLE PASS advertising", offset, timeout=30)
                if network == "A3-LAB":
                    vm.find("GTOS_A3_RADIO WIFI ONLINE ip=", offset, timeout=26)
                    vm.find("GTOS_A3_ORBIT HTTP READY port=8080", offset)
                    vm.find("GTOS_A3_WIRELESS powered=1 sdio=1 wifi=1 ble=1 station=2 ip=", vm.send("s"))
                    expect_status(port, out / "status.json", {
                        "page": 2, "brightness": 60, "encoder": 0, "awake": 1,
                        "imu_result": 0, "accel_mg": [73, -24, 999], "gyro_mdps": [875, -875, 0],
                        "usb_result": 0, "usb_attached": 1, "advertised_ma": 1500,
                        "charger_result": 0, "charging": 0,
                        "sdio_ready": 1, "wifi_online": 1, "ble_advertising": 1,
                    })
                    vm.frame(out / "radio.png", 614)
                    offset = vm.offset()
                    vm.part("part encoder input cw")
                    vm.find("page=2 light=65 awake=1 encoder=1", offset)
                    expect_status(port, out / "status-after-knob.json", {"brightness": 65, "encoder": 1})
                    vm.find("GTOS_A3_CHARGE result=0", vm.send("c"))
                    vm.find("bq=0 pg=1 charge=1", vm.send("s"))
                    expect_status(port, out / "status-charging.json", {"charging": 1})
                else:
                    vm.find("GTOS_A3_RADIO WIFI OFFLINE", offset, timeout=26)
                    vm.find("GTOS_A3_WIRELESS powered=1 sdio=1 wifi=1 ble=1 station=3 ip=", vm.send("s"))
                    retry = vm.send("w")
                    vm.find("GTOS_A3_ORBIT WIFI JOIN result=0", retry)
                    vm.find("GTOS_A3_RADIO WIFI OFFLINE", retry, timeout=26)
                    if b"GTOS_A3_ORBIT HTTP READY" in vm.uart_bytes():
                        raise RuntimeError("Orbit advertised HTTP readiness without a station address")
                if check_guest is not None:
                    check_guest(vm)
                vm.finish()
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    if hci_port is None and b"LE_Set_Advertising_Enable = 1" not in log.read_bytes():
        raise RuntimeError(f"C6 virtual controller did not enable advertising. See {log}")


def run(args):
    out, board = prepare_board(args)
    boot(args, out, board, "A3-LAB")
    missing = out / "missing-network"
    missing.mkdir()
    boot(args, missing, board, "A3-OTHER")
    print("A3 SDIO, Wi-Fi association and HTTP data, absent AP retry, and BLE advertising passed:", out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--c6", type=Path, required=True)
    parser.add_argument("--parts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
